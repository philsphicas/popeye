/*
 * First Move Partition Filter for Intelligent Mode
 *
 * This module provides mechanisms to partition the forward solve phase
 * of intelligent mode by the first move. This allows multiple workers
 * to independently search different portions of the forward solve tree
 * for a SINGLE target position.
 *
 * Two modes are supported:
 *
 * 1. Static Partition Mode (-first-move-partition N/M):
 *    Worker N will only explore first moves where (move_index % M) == (N-1)
 *    Simple but can cause load imbalance if some moves are illegal.
 *
 * 2. Work Queue Mode (-first-move-queue N):
 *    N workers pull moves dynamically from a shared queue.
 *    Automatic load balancing: workers that finish quickly grab more work.
 *
 * This is orthogonal to the existing target position partitioning (king ×
 * checker × check_square). When a single target position takes hours to
 * solve, this allows further subdivision of that work.
 */

#if !defined(OPTIMISATIONS_INTELLIGENT_FIRST_MOVE_PARTITION_H)
#define OPTIMISATIONS_INTELLIGENT_FIRST_MOVE_PARTITION_H

#include "stipulation/stipulation.h"
#include "solving/machinery/solve.h"

/* === Static Partition Mode === */

/* Set first move partition N of M (0-indexed internally)
 * @param index partition index (0 to total-1)
 * @param total total number of first-move partitions
 */
void set_first_move_partition(unsigned int index, unsigned int total);

/* Reset first move partition (disabled) */
void reset_first_move_partition(void);

/* Check if first move partitioning is enabled */
boolean is_first_move_partition_enabled(void);

/* Get current partition index */
unsigned int get_first_move_partition_index(void);

/* Get total partition count */
unsigned int get_first_move_partition_total(void);

/* === Work Queue Mode === */

/* Enable work queue mode and set the queue file descriptor.
 * The queue file contains a single unsigned int: the next move index to assign.
 * Workers use flock() for atomic access.
 * @param fd file descriptor for the queue file (shared across workers via fork)
 */
void set_first_move_work_queue(int fd);

/* Check if work queue mode is enabled */
boolean is_first_move_work_queue_enabled(void);

/* Get the queue file descriptor */
int get_first_move_work_queue_fd(void);

/* === Common Functions === */

/* Get total number of first moves (valid after first combo starts solving)
 * @return total first moves, or 0 if not yet known
 */
unsigned int get_total_first_moves(void);

/* Instrument the solving machinery with the first move partition filter
 * @param si root slice of the solving machinery
 */
void solving_insert_first_move_partition_filter(slice_index si);

/* Try to solve in solve_nr_remaining half-moves.
 * At ply 1 (first move of forward solve), handles move filtering/queuing.
 * @param si slice index
 */
void first_move_partition_filter_solve(slice_index si);

#endif
