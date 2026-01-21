/* Worker mode implementation for structured subprocess output.
 * 
 * See worker.h for protocol documentation.
 */

#include "platform/worker.h"
#include "options/maxsolutions/maxsolutions.h"
#include "options/options.h"
#include <stdio.h>

/* === Worker mode state === */
static boolean worker_mode_enabled = false;

void set_worker_mode(boolean enabled)
{
  worker_mode_enabled = enabled;
}

boolean is_worker_mode(void)
{
  return worker_mode_enabled;
}

/* === Parallel mode state === */
static unsigned int parallel_worker_count = 0;
static int stored_argc = 0;
static char **stored_argv = NULL;
static boolean parallel_done = false;  /* Set when parent completes parallel solving */

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

/* Lifecycle messages - stdout */

void worker_emit_ready(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@READY\n");
    fflush(stderr);
  }
}

void worker_emit_solving(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@SOLVING\n");
    fflush(stderr);
  }
}

void worker_emit_finished(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@FINISHED\n");
    fflush(stderr);
  }
}

void worker_emit_partial(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@PARTIAL\n");
    fflush(stderr);
  }
}

/* Multi-problem messages - stdout */

void worker_emit_problem_start(unsigned int index)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@PROBLEM_START:%u\n", index);
    fflush(stderr);
  }
}

void worker_emit_problem_end(unsigned int index)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@PROBLEM_END:%u\n", index);
    fflush(stderr);
  }
}

/* Solution messages - stdout */

void worker_emit_solution_start(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@SOLUTION_START\n");
    fflush(stderr);
  }
}

void worker_emit_solution_text(char const *line)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@TEXT:%s\n", line);
    fflush(stderr);
  }
}

void worker_emit_solution_end(void)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@SOLUTION_END\n");
    fflush(stderr);
  }
}

/* Timing - stdout */

void worker_emit_time(double seconds)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@TIME:%.3f\n", seconds);
    fflush(stderr);
  }
}

/* Progress messages - stderr */

void worker_emit_heartbeat(unsigned long seconds)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@HEARTBEAT:%lu\n", seconds);
    fflush(stderr);
  }
}

void worker_emit_progress(unsigned int m, unsigned int k, unsigned long positions)
{
  if (worker_mode_enabled)
  {
    fprintf(stderr, "@@PROGRESS:%u+%u:%lu\n", m, k, positions);
    fflush(stderr);
  }
}

/* === Fork-based parallel solving === */

#if defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "optimisations/intelligent/intelligent.h"
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
} worker_info_t;

static worker_info_t *workers = NULL;
static unsigned int num_workers = 0;
static boolean forked_worker = false;
static volatile sig_atomic_t interrupted = 0;
static unsigned int global_solutions_found = 0;

/* Progress aggregation state */
static unsigned int last_printed_depth = 0;
static struct timeval start_time;

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

            printf("%lu potential positions in %u+%u  (Time = %.3f s)\n",
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
            
            printf("%s\n", text);
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
    char const *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '\0') return;

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
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
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
    for (ssize_t i = 0; i < n; i++)
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

boolean is_forked_worker(void)
{
    return forked_worker;
}

boolean parallel_fork_workers(void)
{
    struct timeval end_time;
    unsigned int i;
    int active_workers;

    if (parallel_worker_count == 0)
        return false;

    num_workers = parallel_worker_count;
    if (num_workers > 64) num_workers = 64;

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

    printf("\nParallel solving with %u workers (fork-after-setup)\n\n", num_workers);
    fflush(stdout);

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

            /* DEBUG: test output immediately after fork */
            printf("@@DEBUG:Child %u started\n", i);
            fflush(stdout);

            /* Mark as forked worker */
            forked_worker = true;
            set_worker_mode(true);

            /* Set up partition for this worker */
            set_partition(i - 1, num_workers);

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
    active_workers = (int)num_workers;
    while (active_workers > 0 && !interrupted)
    {
        fd_set readfds;
        int maxfd = 0;
        int ready;
        struct timeval timeout;

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
                        /* Only report when few workers remain */
                        if (active_workers > 0 && active_workers <= 8)
                        {
                            unsigned int j;
                            fprintf(stderr, "[Worker %u finished. Still running (%d): ",
                                    workers[i].partition, active_workers);
                            for (j = 0; j < num_workers; j++)
                                if (!workers[j].finished)
                                    fprintf(stderr, "%u ", workers[j].partition);
                            fprintf(stderr, "]\n");
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

    /* Print total time */
    gettimeofday(&end_time, NULL);
    {
        double elapsed = (double)(end_time.tv_sec - start_time.tv_sec) +
                         (double)(end_time.tv_usec - start_time.tv_usec) / 1000000.0;
        printf("\nTotal parallel time: %.3f s\n", elapsed);
    }

    free(workers);
    workers = NULL;
    parallel_done = true;  /* Mark that parallel solving is complete */

    return true;  /* Parent handled solving */
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

#endif /* Unix/macOS */
