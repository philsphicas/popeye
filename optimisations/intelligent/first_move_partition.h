/*
 * First Move Partition Filter for Intelligent Mode
 *
 * This module provides a mechanism to partition the forward solve phase
 * of intelligent mode by the first move. This allows multiple workers
 * to independently search different portions of the forward solve tree
 * for a SINGLE target position.
 *
 * This is orthogonal to the existing target position partitioning (king ×
 * checker × check_square). When a single target position takes hours to
 * solve, this allows further subdivision of that work.
 *
 * Usage:
 *   -first-move-partition N/M
 *
 * Worker N will only explore first moves where (move_index % M) == (N-1)
 * Combined with -partition, total workers = partition_count × first_move_partition_count
 */

#if !defined(OPTIMISATIONS_INTELLIGENT_FIRST_MOVE_PARTITION_H)
#define OPTIMISATIONS_INTELLIGENT_FIRST_MOVE_PARTITION_H

#include "stipulation/stipulation.h"
#include "solving/machinery/solve.h"

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

/* Instrument the solving machinery with the first move partition filter
 * @param si root slice of the solving machinery
 */
void solving_insert_first_move_partition_filter(slice_index si);

/* Try to solve in solve_nr_remaining half-moves.
 * At ply 1 (first move of forward solve), filters the generated move list
 * to only include moves assigned to this partition.
 * @param si slice index
 */
void first_move_partition_filter_solve(slice_index si);

#endif
