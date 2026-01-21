#ifndef PLATFORM_PARALLEL_FORK_H
#define PLATFORM_PARALLEL_FORK_H

#include "stipulation/stipulation.h"

/* Allocate a parallel worker forker slice */
slice_index alloc_parallel_worker_forker(void);

/* Solve a parallel worker forker slice */
void parallel_worker_forker_solve(slice_index si);

#endif /* PLATFORM_PARALLEL_FORK_H */
