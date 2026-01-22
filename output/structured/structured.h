#if !defined(OUTPUT_STRUCTURED_STRUCTURED_H)
#define OUTPUT_STRUCTURED_STRUCTURED_H

/* Structured output mode for machine-parseable output.
 *
 * This provides a standardized protocol that can be used by:
 * - spinach.tcl (external TCL coordinator)
 * - internal -parallel N mode
 * - any other external parallelization/orchestration tool
 *
 * Protocol messages are emitted to stderr, keeping stdout clean.
 * All protocol lines start with "@@".
 *
 * Lifecycle messages:
 *   @@SOLVING            - started solving
 *   @@FINISHED           - normal completion
 *   @@PARTIAL            - partial completion (maxsol etc)
 *
 * Solution messages:
 *   @@SOLUTION_START     - beginning of solution
 *   @@TEXT:<line>        - solution text line
 *   @@SOLUTION_END       - end of solution
 *
 * Progress messages:
 *   @@HEARTBEAT:<secs>   - periodic heartbeat
 *   @@PROGRESS:<m>+<k>:<positions> - depth/position progress
 *
 * Timing:
 *   @@TIME:<seconds>     - solving time
 */

#include "utilities/boolean.h"

/* === Structured output mode control === */

/* Enable/disable structured output mode */
void set_structured_output_mode(boolean enabled);

/* Check if running in structured output mode */
boolean is_structured_output_mode(void);

/* === Protocol message emissions (to stderr) === */

/* Lifecycle messages */
void structured_output_solving(void);
void structured_output_finished(void);
void structured_output_partial(void);

/* Solution messages */
void structured_output_solution_start(void);
void structured_output_solution_text(char const *line);
void structured_output_solution_end(void);

/* Timing */
void structured_output_time(double seconds);

/* Progress */
void structured_output_heartbeat(unsigned long seconds);
void structured_output_progress(unsigned int m, unsigned int k, unsigned long positions);

#endif /* OUTPUT_STRUCTURED_STRUCTURED_H */
