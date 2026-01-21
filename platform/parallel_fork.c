#include "platform/parallel_fork.h"
#include "platform/worker.h"
#include "stipulation/pipe.h"
#include "solving/pipe.h"
#include "debugging/trace.h"
#include <stdlib.h>

/* Allocate a parallel worker forker slice */
slice_index alloc_parallel_worker_forker(void)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParamListEnd();

  slice_index const result = alloc_pipe(STParallelWorkerForker);

  TraceFunctionExit(__func__);
  TraceFunctionResult("%u", result);
  TraceFunctionResultEnd();
  return result;
}

/* Solve a parallel worker forker slice */
void parallel_worker_forker_solve(slice_index si)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParam("%u", si);
  TraceFunctionParamListEnd();

  /* Check if parallel mode is enabled */
  if (is_parallel_mode())
  {
    /* Attempt to fork workers */
    if (parallel_fork_workers())
    {
      /* Parent process: workers handled solving, so we're done */
      TraceFunctionExit(__func__);
      TraceFunctionResultEnd();
      return;
    }
    /* Child process: continue with normal solving, then exit */
    pipe_solve_delegate(si);
    exit(0);
  }
  else
  {
    /* Not in parallel mode: just delegate normally */
    pipe_solve_delegate(si);
  }

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}
