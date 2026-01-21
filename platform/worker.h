#ifndef PLATFORM_WORKER_H
#define PLATFORM_WORKER_H

/* Worker mode for structured output when running as a subprocess.
 * 
 * This provides a standardized protocol that can be used by:
 * - spinach.tcl (external TCL coordinator)
 * - internal -parallel N mode
 * - any other external parallelization tool
 *
 * Protocol messages (stdout):
 *   @@READY              - worker initialized
 *   @@SOLVING            - started solving
 *   @@SOLUTION_START     - beginning of solution
 *   @@TEXT:<line>        - solution text line
 *   @@SOLUTION_END       - end of solution
 *   @@TIME:<seconds>     - solving time
 *   @@FINISHED           - normal completion
 *   @@PARTIAL            - partial completion (maxsol etc)
 *
 * Progress messages (stderr):
 *   @@HEARTBEAT:<secs>   - periodic heartbeat
 *   @@PROGRESS:<info>    - depth/position progress
 */

#include "utilities/boolean.h"

/* === Worker mode (subprocess) === */

/* Enable/disable worker mode */
void set_worker_mode(boolean enabled);

/* Check if running in worker mode */
boolean is_worker_mode(void);

/* === Worker output emissions === */

/* Lifecycle messages */
void worker_emit_ready(void);
void worker_emit_solving(void);
void worker_emit_finished(void);
void worker_emit_partial(void);

/* Multi-problem messages */
void worker_emit_problem_start(unsigned int index);
void worker_emit_problem_end(unsigned int index);

/* Solution messages */
void worker_emit_solution_start(void);
void worker_emit_solution_text(char const *line);
void worker_emit_solution_end(void);

/* Timing */
void worker_emit_time(double seconds);

/* Progress (stderr) */
void worker_emit_heartbeat(unsigned long seconds);
void worker_emit_progress(unsigned int m, unsigned int k, unsigned long positions);

/* === Parallel mode (parent spawning workers) === */

/* Set number of workers to spawn (0 = disabled) */
void set_parallel_worker_count(unsigned int n);

/* Get number of workers configured */
unsigned int get_parallel_worker_count(void);

/* Store command line arguments for re-exec of workers */
void store_worker_args(int argc, char **argv);

/* Get stored arguments (for fork/exec) */
int get_stored_argc(void);
char **get_stored_argv(void);

/* Check if parallel mode is enabled */
boolean is_parallel_mode(void);

/* === Fork-based parallel solving (Unix/macOS) === */

/* Attempt to fork workers for parallel solving.
 * Returns true if this process handled solving (parent coordinated workers).
 * Returns false if caller should continue with normal solving (either
 * not in parallel mode, or this is a worker child process).
 * @param si slice index (passed to workers' solving)
 */
boolean parallel_fork_workers(void);

/* Check if we're in a forked worker child process */
boolean is_forked_worker(void);

/* Check if parallel parent has completed (workers handled solving) */
boolean parallel_solving_completed(void);

#endif /* PLATFORM_WORKER_H */
