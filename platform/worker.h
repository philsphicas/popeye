#ifndef PLATFORM_WORKER_H
#define PLATFORM_WORKER_H

/* Worker mode for subprocess operation with structured output.
 *
 * This module handles the worker mode flag which enables structured
 * output for subprocess communication.
 *
 * For the structured output protocol itself, see output/structured/structured.h
 * For parallel solving coordination, see platform/parallel.h
 */

#include "utilities/boolean.h"

/* Enable/disable worker mode (also enables structured output) */
void set_worker_mode(boolean enabled);

/* Check if running in worker mode */
boolean is_worker_mode(void);

#endif /* PLATFORM_WORKER_H */
