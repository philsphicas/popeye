/*
 * First Move Partition Filter for Intelligent Mode
 *
 * This module filters the forward solve phase of intelligent mode by the
 * first move index. Two modes are supported:
 *
 * 1. Static Partition Mode (-first-move-partition N/M):
 *    Worker N only explores moves where (move_index % M) == (N-1).
 *    Partition assignment is determined at startup and fixed.
 *
 * 2. Work Queue Mode (-first-move-queue N):
 *    Workers get assigned indices dynamically, then use rotation to
 *    balance load. At each target position, the move assignment rotates
 *    so expensive moves are spread across workers over time. This provides
 *    better load balancing than static partitioning when move costs vary.
 */

#include "optimisations/intelligent/first_move_partition.h"
#include "solving/move_generator.h"
#include "solving/pipe.h"
#include "solving/ply.h"
#include "stipulation/stipulation.h"
#include "stipulation/pipe.h"
#include "stipulation/slice_insertion.h"
#include "stipulation/help_play/branch.h"
#include "debugging/trace.h"
#include "debugging/assert.h"

#include <stdio.h>
#include <string.h>

/* Platform-specific includes for work queue */
#if defined(__unix) || (defined(__APPLE__) && defined(__MACH__))
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <errno.h>
#include <time.h>  /* for nanosleep */
#endif

/* === Static Partition Mode State === */
static unsigned int first_move_partition_index = 0;
static unsigned int first_move_partition_total = 0;

/* === Work Queue Mode State === */
static boolean work_queue_mode = false;
static int work_queue_fd = -1;  /* File descriptor for queue file */

/* === Tracking state === */
static boolean first_move_count_reported = false;
static unsigned int total_first_moves = 0;

/* Set first move partition N of M (0-indexed internally)
 * @param index partition index (0 to total-1)
 * @param total total number of first-move partitions
 */
void set_first_move_partition(unsigned int index, unsigned int total)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParam("%u", index);
  TraceFunctionParam("%u", total);
  TraceFunctionParamListEnd();

  first_move_partition_index = index;
  first_move_partition_total = total;

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

/* Reset first move partition (disabled) */
void reset_first_move_partition(void)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParamListEnd();

  first_move_partition_index = 0;
  first_move_partition_total = 0;

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

/* Check if first move partitioning is enabled */
boolean is_first_move_partition_enabled(void)
{
  return first_move_partition_total > 0;
}

/* Get current partition index */
unsigned int get_first_move_partition_index(void)
{
  return first_move_partition_index;
}

/* Get total partition count */
unsigned int get_first_move_partition_total(void)
{
  return first_move_partition_total;
}

/* Get total number of first moves */
unsigned int get_total_first_moves(void)
{
  return total_first_moves;
}

/* === Work Queue Mode Functions === */

/* Enable work queue mode and set the queue file descriptor.
 * The queue file contains a single unsigned int: the next move index to assign.
 * @param fd file descriptor for the queue file (shared across workers via fork)
 */
void set_first_move_work_queue(int fd)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParam("%d", fd);
  TraceFunctionParamListEnd();

  work_queue_mode = true;
  work_queue_fd = fd;
  /* Disable static partition mode */
  first_move_partition_index = 0;
  first_move_partition_total = 0;

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

/* Check if work queue mode is enabled */
boolean is_first_move_work_queue_enabled(void)
{
  return work_queue_mode;
}

/* Get the queue file descriptor */
int get_first_move_work_queue_fd(void)
{
  return work_queue_fd;
}

/* Queue file format for work queue mode with rotation:
 * Bytes 0-3: next worker index to assign (0, 1, 2, ...)
 * Bytes 4-7: total number of workers
 *
 * Workers get a unique index and use it with rotation to balance load.
 * At each target position, the move assignment rotates so expensive moves
 * are spread across all workers over time.
 */

/* Try to solve in solve_nr_remaining half-moves.
 * At ply 1 (first move of forward solve), filters the generated move list
 * based on the current mode:
 * - Static partition: keeps moves where (index % total) == partition_index
 * - Work queue: rotates move assignment across targets for load balancing
 * @param si slice index
 */
void first_move_partition_filter_solve(slice_index si)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParam("%u", si);
  TraceFunctionParamListEnd();

  /* Check if we're at ply 1 of the forward solve (parent is ply_retro_move) */
  if (parent_ply[nbply] == ply_retro_move)
  {
    numecoup const base = MOVEBASE_OF_PLY(nbply);
    numecoup const top = CURRMOVE_OF_PLY(nbply);
    unsigned int const num_moves = (unsigned int)(top - base);

    /* Record first move count (once per problem) */
    if (!first_move_count_reported)
    {
      total_first_moves = num_moves;
      first_move_count_reported = true;
    }

    /* === WORK QUEUE MODE (Dynamic Work Stealing) === */
    if (work_queue_mode)
    {
      /* Dynamic work stealing with move rotation:
       * 
       * Problem: Some first moves are much more expensive than others.
       * With static assignment, the worker that gets expensive moves is slow.
       * 
       * Solution: Rotate move assignments across targets. If there are N
       * workers and M moves, worker W at target T processes moves where:
       *   (move_index + T) % N == W
       * 
       * This spreads expensive moves across workers over time.
       */
      static int my_worker_index = -1;
      static int total_workers_in_queue = 0;
      static unsigned int target_count = 0;
      
      target_count++;
      
      /* Get worker index on first call */
      if (my_worker_index < 0)
      {
        if (flock(work_queue_fd, LOCK_EX) == 0)
        {
          unsigned int current = 0;
          if (lseek(work_queue_fd, 0, SEEK_SET) >= 0)
          {
            if (read(work_queue_fd, &current, sizeof(current)) == sizeof(current))
            {
              my_worker_index = (int)current;
              current++;
              lseek(work_queue_fd, 0, SEEK_SET);
              write(work_queue_fd, &current, sizeof(current));
              /* Read total workers from second slot */
              if (lseek(work_queue_fd, sizeof(unsigned int), SEEK_SET) >= 0)
              {
                unsigned int total;
                if (read(work_queue_fd, &total, sizeof(total)) == sizeof(total))
                  total_workers_in_queue = (int)total;
              }
            }
          }
          flock(work_queue_fd, LOCK_UN);
        }
        if (my_worker_index < 0 || total_workers_in_queue == 0)
        {
          /* Failed to get assignment */
          pipe_solve_delegate(si);
          TraceFunctionExit(__func__);
          TraceFunctionResultEnd();
          return;
        }
      }
      
      /* Filter moves with rotation: at each target, shift the assignment */
      {
        numecoup new_top = base;
        unsigned int move_idx = 0;
        unsigned int rotation = target_count % (unsigned int)total_workers_in_queue;
        
        for (numecoup i = base + 1; i <= top; ++i, ++move_idx)
        {
          /* Rotate: (move_idx + rotation) % total == my_index */
          if (((move_idx + rotation) % (unsigned int)total_workers_in_queue) 
              == (unsigned int)my_worker_index)
          {
            ++new_top;
            if (new_top != i)
              move_generation_stack[new_top] = move_generation_stack[i];
          }
        }
        
        SET_CURRMOVE(nbply, new_top);
      }
      
      /* Delegate with filtered move list */
      pipe_solve_delegate(si);
      
      TraceFunctionExit(__func__);
      TraceFunctionResultEnd();
      return;
    }

    /* === STATIC PARTITION MODE === */
    if (first_move_partition_total > 0)
    {
      numecoup new_top = base;
      unsigned int move_idx = 0;

      TraceValue("%u", nbply);
      TraceValue("%u", parent_ply[nbply]);
      TraceValue("%u", base);
      TraceValue("%u", top);
      TraceEOL();

      /* Iterate through all generated moves and keep only those in our partition */
      for (numecoup i = base + 1; i <= top; ++i, ++move_idx)
      {
        if ((move_idx % first_move_partition_total) == first_move_partition_index)
        {
          ++new_top;
          if (new_top != i)
            move_generation_stack[new_top] = move_generation_stack[i];
        }
      }

      /* Update the current move pointer to the new top */
      SET_CURRMOVE(nbply, new_top);

      TraceValue("filtered to %u", new_top - base);
      TraceEOL();
    }
  }

  /* Continue solving with the (possibly filtered) move list */
  pipe_solve_delegate(si);

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

/* Callback for slice insertion traversal - inserts after STReadyForHelpMove */
static void insert_first_move_partition_filter(slice_index si,
                                                stip_structure_traversal *st)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParam("%u", si);
  TraceFunctionParamListEnd();

  stip_traverse_structure_children_pipe(si, st);

  {
    slice_index const prototype = alloc_pipe(STFirstMovePartitionFilter);
    help_branch_insert_slices(si, &prototype, 1);
  }

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

/* Instrument the solving machinery with the first move partition filter
 * @param si root slice of the solving machinery
 */
void solving_insert_first_move_partition_filter(slice_index si)
{
  stip_structure_traversal st;

  TraceFunctionEntry(__func__);
  TraceFunctionParam("%u", si);
  TraceFunctionParamListEnd();

  /* Always insert the filter - it reports first move count even when not partitioning */
  stip_structure_traversal_init(&st, NULL);
  stip_structure_traversal_override_single(&st,
                                            STReadyForHelpMove,
                                            &insert_first_move_partition_filter);
  stip_traverse_structure(si, &st);

  TraceStipulation(si);

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}
