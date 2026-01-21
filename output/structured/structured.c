/* Structured output mode implementation.
 *
 * See structured.h for protocol documentation.
 */

#include "output/structured/structured.h"
#include <stdio.h>

/* === Structured output mode state === */
static boolean structured_output_enabled = false;

void set_structured_output_mode(boolean enabled)
{
  structured_output_enabled = enabled;
}

boolean is_structured_output_mode(void)
{
  return structured_output_enabled;
}

/* === Lifecycle messages (stderr) === */

void structured_output_solving(void)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@SOLVING\n");
    fflush(stderr);
  }
}

void structured_output_finished(void)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@FINISHED\n");
    fflush(stderr);
  }
}

void structured_output_partial(void)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@PARTIAL\n");
    fflush(stderr);
  }
}

/* === Solution messages (stderr) === */

void structured_output_solution_start(void)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@SOLUTION_START\n");
    fflush(stderr);
  }
}

void structured_output_solution_text(char const *line)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@TEXT:%s\n", line);
    fflush(stderr);
  }
}

void structured_output_solution_end(void)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@SOLUTION_END\n");
    fflush(stderr);
  }
}

/* === Timing (stderr) === */

void structured_output_time(double seconds)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@TIME:%.3f\n", seconds);
    fflush(stderr);
  }
}

/* === Progress messages (stderr) === */

void structured_output_heartbeat(unsigned long seconds)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@HEARTBEAT:%lu\n", seconds);
    fflush(stderr);
  }
}

void structured_output_progress(unsigned int m, unsigned int k, unsigned long positions)
{
  if (structured_output_enabled)
  {
    fprintf(stderr, "@@PROGRESS:%u+%u:%lu\n", m, k, positions);
    fflush(stderr);
  }
}
