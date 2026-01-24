/* Parallel solving coordination.
 *
 * This module handles:
 * - Parallel mode state (worker count, args)
 * - Fork-based parallel solving (Unix/macOS)
 *
 * For worker mode (structured output), see platform/worker.h
 * For the structured output protocol itself, see output/structured/structured.h
 */

/* Feature test macros must come before any includes */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "platform/parallel.h"
#include "platform/worker.h"
#include "optimisations/intelligent/intelligent.h"
#include "options/maxsolutions/maxsolutions.h"
#include "options/options.h"
#include <stdio.h>

/* === Parallel mode state === */
static unsigned int parallel_worker_count = 0;
static int stored_argc = 0;
static char **stored_argv = NULL;
static boolean parallel_done = false;  /* Set when parent completes parallel solving */

/* === Probe mode state === */
static boolean probe_mode = false;
static unsigned int probe_timeout = 60;  /* Default 60 seconds per partition order */

void set_probe_mode(boolean enabled, unsigned int timeout_secs)
{
  probe_mode = enabled;
  if (timeout_secs > 0)
    probe_timeout = timeout_secs;
}

boolean is_probe_mode(void)
{
  return probe_mode;
}

void set_parallel_worker_count(unsigned int n)
{
  parallel_worker_count = n;
}

unsigned int get_parallel_worker_count(void)
{
  return parallel_worker_count;
}

void store_worker_args(int argc, char **argv)
{
  stored_argc = argc;
  stored_argv = argv;
}

int get_stored_argc(void)
{
  return stored_argc;
}

char **get_stored_argv(void)
{
  return stored_argv;
}

boolean is_parallel_mode(void)
{
  return parallel_worker_count > 0;
}

boolean parallel_solving_completed(void)
{
  return parallel_done;
}

/* === Fork-based parallel solving === */

#if defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/select.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

/* Progress aggregation constants */
#define MAX_DEPTH_TRACKED 10000  /* Supports M*100+K for M,K up to 99 */
#define ENCODE_DEPTH(m, k) ((m) * 100 + (k))
#define DECODE_M(d) ((d) / 100)
#define DECODE_K(d) ((d) % 100)

/* Worker tracking */
typedef struct {
    pid_t pid;
    int pipe_fd;
    unsigned int partition;
    char line_buffer[8192];
    size_t buffer_pos;
    boolean finished;
    /* Progress tracking for aggregation */
    unsigned int last_depth;
    unsigned long positions_at_depth[MAX_DEPTH_TRACKED];
    /* Current combo being worked on */
    char current_combo[64];
} worker_info_t;

static worker_info_t *workers = NULL;
static unsigned int num_workers = 0;
static boolean forked_worker = false;
static volatile sig_atomic_t interrupted = 0;
static unsigned int global_solutions_found = 0;

/* Progress aggregation state */
static unsigned int last_printed_depth = 0;
static struct timeval start_time;

/* Probe mode: heavy combo tracking */
#define MAX_HEAVY_COMBOS 256
typedef struct {
    char combo_info[64];      /* e.g., "23802 king=c8 checker=Pd6 checksq=d7" */
    unsigned int seen_count;  /* How many partition orders saw this as heavy */
    unsigned int max_depth;   /* Maximum depth reached before timeout */
} heavy_combo_t;

static heavy_combo_t heavy_combos[MAX_HEAVY_COMBOS];
static unsigned int num_heavy_combos = 0;

/* Extract combo number from combo_info string (first number) */
static unsigned int extract_combo_number(char const *info)
{
    unsigned int num = 0;
    while (*info >= '0' && *info <= '9')
    {
        num = num * 10 + (unsigned int)(*info - '0');
        info++;
    }
    return num;
}

/* Record a heavy combo (one that didn't finish in time) */
static void record_heavy_combo(char const *combo_info, unsigned int depth)
{
    unsigned int i;
    unsigned int combo_num = extract_combo_number(combo_info);
    
    /* Check if we already have this combo */
    for (i = 0; i < num_heavy_combos; i++)
    {
        if (extract_combo_number(heavy_combos[i].combo_info) == combo_num)
        {
            heavy_combos[i].seen_count++;
            if (depth > heavy_combos[i].max_depth)
                heavy_combos[i].max_depth = depth;
            return;
        }
    }
    
    /* Add new heavy combo */
    if (num_heavy_combos < MAX_HEAVY_COMBOS)
    {
        strncpy(heavy_combos[num_heavy_combos].combo_info, combo_info, 63);
        heavy_combos[num_heavy_combos].combo_info[63] = '\0';
        heavy_combos[num_heavy_combos].seen_count = 1;
        heavy_combos[num_heavy_combos].max_depth = depth;
        num_heavy_combos++;
    }
}

static void kill_all_workers(void)
{
    unsigned int i;
    for (i = 0; i < num_workers; i++)
    {
        if (workers[i].pid > 0 && !workers[i].finished)
        {
            kill(workers[i].pid, SIGTERM);
            workers[i].finished = true;
        }
    }
}

static void signal_handler(int sig)
{
    interrupted = 1;
    /* Kill all worker processes */
    if (workers != NULL)
    {
        unsigned int i;
        for (i = 0; i < num_workers; i++)
        {
            if (workers[i].pid > 0 && !workers[i].finished)
                kill(workers[i].pid, sig);
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void handle_progress(worker_info_t *w, unsigned int m, unsigned int k, unsigned long positions)
{
    unsigned int depth = ENCODE_DEPTH(m, k);
    if (depth < MAX_DEPTH_TRACKED)
    {
        w->positions_at_depth[depth] = positions;
        w->last_depth = depth;
    }

    /* Only print progress if movenumbers option is enabled */
    if (!OptFlag[movenbr])
        return;

    /* Check if all workers have reached this depth */
    if (depth > last_printed_depth && workers != NULL)
    {
        unsigned int i;
        unsigned int min_depth = depth;
        unsigned long total_positions = 0;

        for (i = 0; i < num_workers; i++)
        {
            if (!workers[i].finished && workers[i].last_depth < min_depth)
                min_depth = workers[i].last_depth;
        }

        /* Print all depths from last_printed+1 to min_depth */
        while (last_printed_depth < min_depth)
        {
            unsigned int d = last_printed_depth + 1;
            unsigned int dm = DECODE_M(d), dk = DECODE_K(d);
            struct timeval now;
            double elapsed;

            total_positions = 0;
            for (i = 0; i < num_workers; i++)
                total_positions += workers[i].positions_at_depth[d];

            gettimeofday(&now, NULL);
            elapsed = (double)(now.tv_sec - start_time.tv_sec) +
                      (double)(now.tv_usec - start_time.tv_usec) / 1000000.0;

            /* Match original style: leading newline, no trailing (cursor stays at end) */
            printf("\n%lu potential positions in %u+%u  (Time = %.3f s)",
                   total_positions, dm, dk, elapsed);
            fflush(stdout);

            last_printed_depth = d;
        }
    }
}

static void process_worker_line(worker_info_t *w, char const *line)
{
    /* Check if line contains @@ protocol marker anywhere */
    char *protocol_start = strstr(line, "@@");
    if (protocol_start != NULL)
    {
        /* Handle the protocol part */
        if (strncmp(protocol_start, "@@PROGRESS:", 11) == 0)
        {
            unsigned int m, k;
            unsigned long positions;
            if (sscanf(protocol_start + 11, "%u+%u:%lu", &m, &k, &positions) == 3)
                handle_progress(w, m, k, positions);
        }
        else if (strncmp(protocol_start, "@@TEXT:", 7) == 0)
        {
            char const *text = protocol_start + 7;
            
            /* Skip whitespace-only TEXT messages */
            char const *p = text;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '\0')
                return;  /* Don't print whitespace-only text */
            
            /* Match original style: leading newline, no trailing */
            printf("\n%s", text);
            fflush(stdout);

            /* Check if this is a solution line */
            while (*text == ' ') text++;
            if (text[0] >= '1' && text[0] <= '9' && text[1] == '.')
            {
                global_solutions_found++;
                if (get_max_solutions_per_phase() < UINT_MAX && global_solutions_found >= get_max_solutions_per_phase())
                {
                    kill_all_workers();
                }
            }
        }
        else if (strncmp(protocol_start, "@@FINISHED", 10) == 0)
        {
            /* Worker finished - already tracked by pipe close */
        }
        else if (strncmp(protocol_start, "@@COMBO:", 8) == 0)
        {
            /* Store current combo info for status display */
            char const *info = protocol_start + 8;
            strncpy(w->current_combo, info, sizeof(w->current_combo) - 1);
            w->current_combo[sizeof(w->current_combo) - 1] = '\0';
            /* Remove trailing newline if present */
            char *nl = strchr(w->current_combo, '\n');
            if (nl) *nl = '\0';
        }
        else if (strncmp(protocol_start, "@@DEBUG:", 8) == 0)
        {
            /* Debug messages - suppress in production */
        }
        /* Other @@ messages can be aggregated/handled as needed */
        return;  /* Don't print the raw line */
    }

    /* Filter out stipulation echo */
    if (strncmp(line, "ser-", 4) == 0 || strncmp(line, "  ser-", 6) == 0)
        return;

    /* Skip blank or whitespace-only lines */
    {
        char const *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') return;
    }

    /* Filter out worker "solution finished" noise */
    if (strncmp(line, "solution finished", 17) == 0)
        return;

    /* Non-protocol output - solutions, etc. */
    printf("%s\n", line);
    fflush(stdout);
}

static void process_worker_output(worker_info_t *w)
{
    char buf[4096];
    ssize_t n;

    n = read(w->pipe_fd, buf, sizeof(buf) - 1);
    if (n <= 0)
    {
        /* EAGAIN and EWOULDBLOCK are the same on Linux, separate check avoids warning */
        boolean is_blocking_error = (errno == EAGAIN);
#if EAGAIN != EWOULDBLOCK
        is_blocking_error = is_blocking_error || (errno == EWOULDBLOCK);
#endif
        if (n == 0 || !is_blocking_error)
        {
            w->finished = true;
            if (w->buffer_pos > 0)
            {
                w->line_buffer[w->buffer_pos] = '\0';
                process_worker_line(w, w->line_buffer);
                w->buffer_pos = 0;
            }
        }
        return;
    }

    buf[n] = '\0';
    {
        ssize_t i;
        for (i = 0; i < n; i++)
        {
            if (buf[i] == '\n')
            {
                w->line_buffer[w->buffer_pos] = '\0';
                process_worker_line(w, w->line_buffer);
                w->buffer_pos = 0;
            }
            else if (buf[i] != '\r' && w->buffer_pos < sizeof(w->line_buffer) - 1)
            {
                w->line_buffer[w->buffer_pos++] = buf[i];
            }
        }
    }
}

boolean is_forked_worker(void)
{
    return forked_worker;
}

boolean parallel_fork_workers(void)
{
    unsigned int i;
    int active_workers;
    struct timeval last_status_time;

    if (parallel_worker_count == 0)
        return false;

    num_workers = parallel_worker_count;
    if (num_workers > 1024) num_workers = 1024;

    gettimeofday(&start_time, NULL);
    last_printed_depth = ENCODE_DEPTH(1, 0);  /* Start before 1+1 */

    workers = calloc(num_workers, sizeof(worker_info_t));
    if (!workers)
    {
        fprintf(stderr, "Failed to allocate worker array\n");
        return false;
    }

    for (i = 0; i < num_workers; i++)
    {
        workers[i].pipe_fd = -1;
        workers[i].last_depth = 0;
    }

    /* Install signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    fprintf(stderr, "\nUsing %u parallel workers (partition order: %s)\n", num_workers, partition_order);
    fflush(stderr);

    /* Fork workers */
    for (i = 1; i <= num_workers; i++)
    {
        int pipefd[2];
        pid_t pid;

        if (pipe(pipefd) < 0)
        {
            perror("pipe");
            continue;
        }

        pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }

        if (pid == 0)
        {
            /* === CHILD PROCESS === */
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);

            /* Force stdio to use the new file descriptors */
            setvbuf(stdout, NULL, _IOLBF, 0);
            setvbuf(stderr, NULL, _IOLBF, 0);

            /* Mark as forked worker */
            forked_worker = true;
            set_worker_mode(true);

            /* Set up strided partition for this worker.
             * With N workers and 61440 total combos, worker i handles:
             * combos i-1, i-1+N, i-1+2N, ... (stride = N)
             * This distributes heavy combos across all workers.
             */
            set_partition_range(i - 1, num_workers, 61440);

            /* Reset signal handlers */
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);

            /* Clean up parent's worker array */
            free(workers);
            workers = NULL;
            num_workers = 0;

            /* Return false so caller continues with normal solving */
            return false;
        }

        /* === PARENT PROCESS === */
        close(pipefd[1]);

        workers[i-1].pid = pid;
        workers[i-1].pipe_fd = pipefd[0];
        workers[i-1].partition = i;
        workers[i-1].buffer_pos = 0;
        workers[i-1].finished = false;

        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    }

    /* Parent: collect output from all workers */
    /* Count only workers that were successfully forked */
    active_workers = 0;
    for (i = 0; i < num_workers; i++)
        if (workers[i].pid > 0)
            active_workers++;
    
    if (active_workers < (int)num_workers)
        fprintf(stderr, "Warning: only %d of %u workers started (fork/pipe limit?)\n",
                active_workers, num_workers);
    
    last_status_time = start_time;
    while (active_workers > 0 && !interrupted)
    {
        fd_set readfds;
        int maxfd = 0;
        int ready;
        struct timeval timeout;
        struct timeval now;

        FD_ZERO(&readfds);
        for (i = 0; i < num_workers; i++)
        {
            if (!workers[i].finished && workers[i].pipe_fd >= 0)
            {
                FD_SET(workers[i].pipe_fd, &readfds);
                if (workers[i].pipe_fd > maxfd)
                    maxfd = workers[i].pipe_fd;
            }
        }

        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);

        /* Periodic status update every 10 seconds if workers still running */
        gettimeofday(&now, NULL);
        if (active_workers > 0)
        {
            double since_last = (double)(now.tv_sec - last_status_time.tv_sec) +
                                (double)(now.tv_usec - last_status_time.tv_usec) / 1000000.0;
            if (since_last >= 10.0)
            {
                double elapsed = (double)(now.tv_sec - start_time.tv_sec) +
                                 (double)(now.tv_usec - start_time.tv_usec) / 1000000.0;
                fprintf(stderr, "\n[%.0fs: %d/%u workers running",
                        elapsed, active_workers, num_workers);
                /* Only list individual workers if few remain */
                if (active_workers <= 16)
                {
                    fprintf(stderr, "]\n");
                    for (i = 0; i < num_workers; i++)
                        if (!workers[i].finished && workers[i].pid > 0)
                        {
                            if (workers[i].current_combo[0])
                                fprintf(stderr, "  W%u: %s\n", workers[i].partition, workers[i].current_combo);
                            else
                                fprintf(stderr, "  W%u: (starting)\n", workers[i].partition);
                        }
                }
                else
                    fprintf(stderr, "]");
                fflush(stderr);
                last_status_time = now;
            }
        }

        if (ready > 0)
        {
            for (i = 0; i < num_workers; i++)
            {
                if (!workers[i].finished && workers[i].pipe_fd >= 0 &&
                    FD_ISSET(workers[i].pipe_fd, &readfds))
                {
                    process_worker_output(&workers[i]);
                    if (workers[i].finished)
                    {
                        close(workers[i].pipe_fd);
                        workers[i].pipe_fd = -1;
                        active_workers--;
                        /* Always report worker completion with timestamp */
                        if (active_workers >= 0)
                        {
                            unsigned int j;
                            double elapsed = (double)(now.tv_sec - start_time.tv_sec) +
                                             (double)(now.tv_usec - start_time.tv_usec) / 1000000.0;
                            fprintf(stderr, "\n[%.0fs: Worker %u/%u finished. Still running (%d): ",
                                    elapsed, workers[i].partition, num_workers, active_workers);
                            for (j = 0; j < num_workers; j++)
                                if (!workers[j].finished && workers[j].pid > 0)
                                    fprintf(stderr, "%u ", workers[j].partition);
                            fprintf(stderr, "]");
                            fflush(stderr);
                            last_status_time = now;
                        }
                    }
                }
            }
        }
    }

    /* Wait for all children and flush any remaining output */
    for (i = 0; i < num_workers; i++)
    {
        if (workers[i].pid > 0)
            waitpid(workers[i].pid, NULL, 0);
        /* Read any remaining output after child exit */
        if (workers[i].pipe_fd >= 0)
        {
            /* Set to blocking for final read */
            int flags = fcntl(workers[i].pipe_fd, F_GETFL, 0);
            fcntl(workers[i].pipe_fd, F_SETFL, flags & ~O_NONBLOCK);
            while (!workers[i].finished)
                process_worker_output(&workers[i]);
            close(workers[i].pipe_fd);
            workers[i].pipe_fd = -1;
        }
    }

    free(workers);
    workers = NULL;
    parallel_done = true;  /* Mark that parallel solving is complete */

    return true;  /* Parent handled solving */
}

/* Run a single probe phase with the given partition order.
 * Returns the number of workers that finished within the timeout.
 * Records heavy combos (workers still running at timeout).
 */
static int run_probe_phase(char const *order, unsigned int timeout_secs)
{
    unsigned int i;
    int active_workers;
    int completed_workers = 0;
    struct timeval phase_start, now;
    double elapsed;
    
    /* Set the partition order */
    set_partition_order(order);
    
    gettimeofday(&phase_start, NULL);
    last_printed_depth = ENCODE_DEPTH(1, 0);
    
    workers = calloc(num_workers, sizeof(worker_info_t));
    if (!workers)
    {
        fprintf(stderr, "Failed to allocate worker array\n");
        return 0;
    }
    
    for (i = 0; i < num_workers; i++)
    {
        workers[i].pipe_fd = -1;
        workers[i].last_depth = 0;
    }
    
    fprintf(stderr, "  Probing with partition order '%s' (timeout %us)...\n", order, timeout_secs);
    fflush(stderr);
    
    /* Fork workers */
    for (i = 1; i <= num_workers; i++)
    {
        int pipefd[2];
        pid_t pid;
        
        if (pipe(pipefd) < 0)
            continue;
        
        pid = fork();
        if (pid < 0)
        {
            close(pipefd[0]);
            close(pipefd[1]);
            continue;
        }
        
        if (pid == 0)
        {
            /* === CHILD PROCESS === */
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            
            setvbuf(stdout, NULL, _IOLBF, 0);
            setvbuf(stderr, NULL, _IOLBF, 0);
            
            forked_worker = true;
            set_worker_mode(true);
            set_partition_range(i - 1, num_workers, 61440);
            
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            
            free(workers);
            workers = NULL;
            num_workers = 0;
            
            return -1;  /* Signal to caller: this is child, continue solving */
        }
        
        /* === PARENT PROCESS === */
        close(pipefd[1]);
        
        workers[i-1].pid = pid;
        workers[i-1].pipe_fd = pipefd[0];
        workers[i-1].partition = i;
        workers[i-1].buffer_pos = 0;
        workers[i-1].finished = false;
        
        fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    }
    
    /* Count successfully forked workers */
    active_workers = 0;
    for (i = 0; i < num_workers; i++)
        if (workers[i].pid > 0)
            active_workers++;
    
    /* Run until timeout or all workers done */
    while (active_workers > 0 && !interrupted)
    {
        fd_set readfds;
        int maxfd = 0;
        int ready;
        struct timeval timeout;
        
        gettimeofday(&now, NULL);
        elapsed = (double)(now.tv_sec - phase_start.tv_sec) +
                  (double)(now.tv_usec - phase_start.tv_usec) / 1000000.0;
        
        /* Check if timeout reached */
        if (elapsed >= (double)timeout_secs)
        {
            /* Record heavy combos from workers still running */
            for (i = 0; i < num_workers; i++)
            {
                if (!workers[i].finished && workers[i].pid > 0)
                {
                    if (workers[i].current_combo[0])
                        record_heavy_combo(workers[i].current_combo, workers[i].last_depth);
                }
            }
            break;
        }
        
        FD_ZERO(&readfds);
        for (i = 0; i < num_workers; i++)
        {
            if (!workers[i].finished && workers[i].pipe_fd >= 0)
            {
                FD_SET(workers[i].pipe_fd, &readfds);
                if (workers[i].pipe_fd > maxfd)
                    maxfd = workers[i].pipe_fd;
            }
        }
        
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        ready = select(maxfd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ready > 0)
        {
            for (i = 0; i < num_workers; i++)
            {
                if (!workers[i].finished && workers[i].pipe_fd >= 0 &&
                    FD_ISSET(workers[i].pipe_fd, &readfds))
                {
                    process_worker_output(&workers[i]);
                    if (workers[i].finished)
                    {
                        close(workers[i].pipe_fd);
                        workers[i].pipe_fd = -1;
                        active_workers--;
                        completed_workers++;
                    }
                }
            }
        }
    }
    
    /* Kill remaining workers */
    kill_all_workers();
    
    /* Wait for all children */
    for (i = 0; i < num_workers; i++)
    {
        if (workers[i].pid > 0)
            waitpid(workers[i].pid, NULL, 0);
        if (workers[i].pipe_fd >= 0)
        {
            close(workers[i].pipe_fd);
            workers[i].pipe_fd = -1;
        }
    }
    
    gettimeofday(&now, NULL);
    elapsed = (double)(now.tv_sec - phase_start.tv_sec) +
              (double)(now.tv_usec - phase_start.tv_usec) / 1000000.0;
    
    fprintf(stderr, "    Completed: %d workers, Still running at timeout: %d (%.1fs)\n",
            completed_workers, (int)num_workers - completed_workers - (num_workers > 0 ? 0 : 0),
            elapsed);
    
    free(workers);
    workers = NULL;
    
    return completed_workers;
}

/* Print probe summary */
static void print_probe_summary(void)
{
    unsigned int i;
    
    fprintf(stderr, "\n=== PROBE SUMMARY ===\n");
    fprintf(stderr, "Total combos: 61440\n");
    fprintf(stderr, "Heavy combos identified: %u\n\n", num_heavy_combos);
    
    if (num_heavy_combos > 0)
    {
        /* Sort by seen_count descending */
        for (i = 0; i < num_heavy_combos; i++)
        {
            unsigned int j;
            for (j = i + 1; j < num_heavy_combos; j++)
            {
                if (heavy_combos[j].seen_count > heavy_combos[i].seen_count)
                {
                    heavy_combo_t tmp = heavy_combos[i];
                    heavy_combos[i] = heavy_combos[j];
                    heavy_combos[j] = tmp;
                }
            }
        }
        
        for (i = 0; i < num_heavy_combos; i++)
        {
            fprintf(stderr, "HEAVY %s (seen %u times, max depth %u+%u)\n",
                    heavy_combos[i].combo_info,
                    heavy_combos[i].seen_count,
                    DECODE_M(heavy_combos[i].max_depth),
                    DECODE_K(heavy_combos[i].max_depth));
        }
    }
    else
    {
        fprintf(stderr, "(No heavy combos found - all work completed quickly)\n");
    }
    
    fprintf(stderr, "\n");
    fflush(stderr);
}

/* Run probe mode: cycle through partition orders and identify heavy combos */
boolean parallel_probe(void)
{
    static char const *orders[] = {"kpc", "kcp", "pkc", "pck", "ckp", "cpk"};
    unsigned int num_orders = sizeof(orders) / sizeof(orders[0]);
    unsigned int i;
    int result;
    
    if (!probe_mode || parallel_worker_count == 0)
        return false;
    
    num_workers = parallel_worker_count;
    if (num_workers > 1024) num_workers = 1024;
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    fprintf(stderr, "\n=== PROBE MODE ===\n");
    fprintf(stderr, "Workers: %u, Timeout per order: %us\n\n", num_workers, probe_timeout);
    fflush(stderr);
    
    gettimeofday(&start_time, NULL);
    
    for (i = 0; i < num_orders && !interrupted; i++)
    {
        result = run_probe_phase(orders[i], probe_timeout);
        if (result < 0)
        {
            /* This is a child process - continue with solving */
            return false;
        }
    }
    
    print_probe_summary();
    
    parallel_done = true;
    return true;  /* Parent handled probing */
}

#else
/* Non-Unix stub */

boolean is_forked_worker(void)
{
    return false;
}

boolean parallel_fork_workers(void)
{
    if (parallel_worker_count > 0)
    {
        fprintf(stderr, "Parallel solving not supported on this platform\n");
    }
    return false;
}

boolean parallel_probe(void)
{
    if (probe_mode && parallel_worker_count > 0)
    {
        fprintf(stderr, "Probe mode not supported on this platform\n");
    }
    return false;
}

#endif /* Unix/macOS */
