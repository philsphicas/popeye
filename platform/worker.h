#ifndef PLATFORM_WORKER_H
#define PLATFORM_WORKER_H

/* Worker mode for subprocess operation with structured output.
 *
 * This module handles the platform-specific aspects of running as a
 * worker subprocess or coordinating parallel workers.
 *
 * For the structured output protocol itself, see output/structured/structured.h
 */

#include "utilities/boolean.h"

/* === Worker mode (subprocess) === */

/* Enable/disable worker mode (also enables structured output) */
void set_worker_mode(boolean enabled);

/* Check if running in worker mode */
boolean is_worker_mode(void);

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
 */
boolean parallel_fork_workers(void);

/* Check if we're in a forked worker child process */
boolean is_forked_worker(void);

/* Check if parallel parent has completed (workers handled solving) */
boolean parallel_solving_completed(void);

#endif /* PLATFORM_WORKER_H */
