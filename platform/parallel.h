#ifndef PLATFORM_PARALLEL_H
#define PLATFORM_PARALLEL_H

/* Parallel solving coordination.
 *
 * This module handles:
 * - Parallel mode state (worker count, args)
 * - Fork-based parallel solving (Unix/macOS)
 *
 * For worker mode (structured output), see platform/worker.h
 */

#include "utilities/boolean.h"

/* === Parallel mode configuration === */

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

/* === Probe mode === */

/* Enable probe mode with optional timeout per partition order (default 60s) */
void set_probe_mode(boolean enabled, unsigned int timeout_secs);

/* Check if probe mode is enabled */
boolean is_probe_mode(void);

/* === Rebalance mode === */

/* Enable rebalance mode: after timeout, kill slow workers and restart
 * heavy combos with first-move partitioning across available workers.
 */
void set_rebalance_mode(boolean enabled, unsigned int timeout_secs);

/* Check if rebalance mode is enabled */
boolean is_rebalance_mode(void);

/* Get rebalance timeout in seconds */
unsigned int get_rebalance_timeout(void);

/* === First-Move Work Queue Mode === */

/* Enable first-move work queue mode with specified worker count.
 * In this mode, workers dynamically pull first moves from a shared queue,
 * providing automatic load balancing.
 * @param count number of workers to use (0 = disabled)
 */
void set_first_move_queue_mode(unsigned int count);

/* Get the configured first-move queue worker count (0 = disabled) */
unsigned int get_first_move_queue_count(void);

/* Check if first-move work queue mode is enabled */
boolean is_first_move_queue_mode(void);

/* Fork workers for first-move work queue.
 * Creates a shared queue file and forks workers.
 * Returns true if this process handled solving (parent coordinated workers).
 * Returns false if caller should continue with normal solving (either
 * not in queue mode, or this is a worker child process).
 */
boolean parallel_first_move_queue(void);

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

/* Run probe mode: cycle through partition orders to identify heavy combos.
 * Returns true if probing was handled (parent process).
 * Returns false if caller should continue solving (child worker or not in probe mode).
 */
boolean parallel_probe(void);

#endif /* PLATFORM_PARALLEL_H */
