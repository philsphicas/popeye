/* Worker mode implementation for subprocess operation.
 *
 * This module handles the worker mode flag which enables structured
 * output for subprocess communication.
 *
 * For the structured output protocol itself, see output/structured/structured.h
 * For parallel solving coordination, see platform/parallel.h
 */

#include "platform/worker.h"
#include "output/structured/structured.h"

/* === Worker mode state === */
static boolean worker_mode_enabled = false;

void set_worker_mode(boolean enabled)
{
  worker_mode_enabled = enabled;
  /* Worker mode implies structured output */
  set_structured_output_mode(enabled);
}

boolean is_worker_mode(void)
{
  return worker_mode_enabled;
}
