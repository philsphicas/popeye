#include "input/commandline.h"
#include "optimisations/hash.h"
#include "optimisations/intelligent/intelligent.h"
#include "optimisations/intelligent/first_move_partition.h"
#include "output/plaintext/language_dependant.h"
#include "output/plaintext/protocol.h"
#include "platform/maxtime.h"
#include "platform/heartbeat.h"
#include "platform/maxmem.h"
#include "platform/worker.h"
#include "platform/parallel.h"
#include "options/options.h"
#include "input/plaintext/memory.h"
#include "stipulation/pipe.h"
#include "solving/pipe.h"
#include "debugging/trace.h"

#include <stdlib.h>
#include <string.h>

static int parseCommandlineOptions(int argc, char *argv[])
{
  int idx = 1;

  TraceFunctionEntry(__func__);
  TraceFunctionParamListEnd();

  while (idx<argc)
  {
    TraceValue("%d",idx);
    TraceValue("%s",argv[idx]);
    TraceEOL();

#if defined(FXF)
    if (idx+1<argc && strcmp(argv[idx], "-maxpos")==0)
    {
      char *end;
      idx++;
      hash_max_kilo_storable_positions = strtoul(argv[idx], &end, 10);
      if (argv[idx]==end)
      {
        /* conversion failure
         * -> set to 0 now and to default value later */
        hash_max_kilo_storable_positions = 0;
      }
      idx++;
      continue;
    }
    else
#endif
    if (idx+1<argc && strcmp(argv[idx], "-maxtime")==0)
    {
      char *end;
      maxtime_type value;
      idx++;
      value = (unsigned int)strtoul(argv[idx], &end, 10);
      if (argv[idx]==end)
        ; /* conversion failure -> assume no max time */
      else
        platform_set_commandline_maxtime(value);

      idx++;
      continue;
    }
    if (strcmp(argv[idx], "-heartbeat")==0)
    {
      char *end;
      heartbeat_type value;

      if (idx+1<argc)
      {
        value = (unsigned int)strtoul(argv[idx+1], &end, 10);
        if (argv[idx+1]==end)
        {
          /* conversion failure -> assume default heartbeat rate */
          value = heartbeat_default_rate;
        }
        else
          idx++;
      }
      else
        value = heartbeat_default_rate;

      platform_set_commandline_heartbeat(value);

      idx++;
      continue;
    }
#if defined(FXF)
    else if (idx+1<argc && strcmp(argv[idx],"-maxmem")==0)
    {
      input_plaintext_read_requested_memory(argv[idx+1]);
      idx += 2;
      continue;
    }
#endif
    else if (strcmp(argv[idx], "-regression")==0)
    {
      protocol_overwrite();
      output_plaintext_suppress_variable();
      idx++;
      continue;
    }
    else if (strcmp(argv[idx], "-nogreeting")==0)
    {
      output_plaintext_suppress_greeting();
      idx++;
      continue;
    }
    else if (strcmp(argv[idx], "-worker")==0)
    {
      /* Worker mode: structured output for subprocess coordination */
      set_worker_mode(true);
      output_plaintext_suppress_greeting();
      OptFlag[noboard] = true;  /* Suppress board diagram */
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-parallel")==0)
    {
      /* Parallel mode: spawn N workers with king-partitioned search */
      char *end;
      unsigned long n;
      idx++;
      n = strtoul(argv[idx], &end, 10);
      if (*end == '\0' && n > 0 && n <= 1024)
      {
        set_parallel_worker_count((unsigned int)n);
      }
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-partition-order")==0)
    {
      /* Set partition order: kpc, cpk, pck, etc.
       * First char varies fastest (distributed across workers first).
       */
      idx++;
      set_partition_order(argv[idx]);
      idx++;
      continue;
    }
    else if (strcmp(argv[idx], "-probe")==0)
    {
      /* Probe mode: cycle through partition orders to discover heavy combos.
       * Optional argument is timeout in seconds (default 60).
       */
      unsigned int timeout = 60;
      idx++;
      if (idx < argc && argv[idx][0] != '-')
      {
        char *end;
        unsigned long t = strtoul(argv[idx], &end, 10);
        if (*end == '\0' && t > 0 && t <= 3600)
        {
          timeout = (unsigned int)t;
          idx++;
        }
      }
      set_probe_mode(true, timeout);
      continue;
    }
    else if (strcmp(argv[idx], "-rebalance")==0)
    {
      /* Rebalance mode: after timeout, kill slow workers and restart with
       * first-move partitioning. Optional argument is timeout in seconds.
       */
      unsigned int timeout = 60;
      idx++;
      if (idx < argc && argv[idx][0] != '-')
      {
        char *end;
        unsigned long t = strtoul(argv[idx], &end, 10);
        if (*end == '\0' && t > 0 && t <= 3600)
        {
          timeout = (unsigned int)t;
          idx++;
        }
      }
      set_rebalance_mode(true, timeout);
      continue;
    }
    else if (strcmp(argv[idx], "-maxtrace")==0)
    {
#if defined(DOTRACE)
      trace_level max_trace_level;
      char *end;

      idx++;
      if (idx<argc)
      {
        max_trace_level = strtoul(argv[idx], &end, 10);
        if (*end==0)
          TraceSetMaxLevel(max_trace_level);
        else
        {
          /* conversion failure  - ignore option */
        }
      }
#else
      /* ignore the value*/
      idx++;
#endif

      idx++;
      continue;
    }
    else if (strcmp(argv[idx], "-notraceptr")==0)
    {
      TraceSuppressPointerValues();
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-partition")==0)
    {
      /* Parse N/M format for partition (1-indexed, user-friendly)
       * Example: -partition 1/4 means partition 1 of 4
       * Internally converted to 0-indexed for set_partition()
       */
      char *slash;
      idx++;
      slash = strchr(argv[idx], '/');
      if (slash != NULL)
      {
        char *end;
        unsigned long n, m;
        n = strtoul(argv[idx], &end, 10);
        if (end == slash)
        {
          m = strtoul(slash + 1, &end, 10);
          if (*end == '\0' && n >= 1 && n <= m && m > 0)
          {
            /* Convert from 1-indexed to 0-indexed */
            set_partition((unsigned int)(n - 1), (unsigned int)m);
          }
        }
      }
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-first-move-partition")==0)
    {
      /* Parse N/M format for first move partition (1-indexed, user-friendly)
       * Example: -first-move-partition 1/4 means worker 1 of 4
       * Worker N only explores first moves where (move_index % M) == (N-1)
       * Internally converted to 0-indexed for set_first_move_partition()
       */
      char *slash;
      idx++;
      slash = strchr(argv[idx], '/');
      if (slash != NULL)
      {
        char *end;
        unsigned long n, m;
        n = strtoul(argv[idx], &end, 10);
        if (end == slash)
        {
          m = strtoul(slash + 1, &end, 10);
          if (*end == '\0' && n >= 1 && n <= m && m > 0)
          {
            /* Convert from 1-indexed to 0-indexed */
            set_first_move_partition((unsigned int)(n - 1), (unsigned int)m);
          }
        }
      }
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-first-move-queue")==0)
    {
      /* Work queue mode for first moves with N workers.
       * Workers dynamically pull first moves from a shared queue,
       * providing automatic load balancing.
       * Example: -first-move-queue 4 uses 4 workers
       */
      char *end;
      unsigned long n;
      idx++;
      n = strtoul(argv[idx], &end, 10);
      if (*end == '\0' && n >= 1 && n <= 1024)
      {
        set_first_move_queue_mode((unsigned int)n);
      }
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-single-combo")==0)
    {
      /* Single combo mode: only process one specific combo index
       * Used internally by rebalancing to target heavy combos
       */
      char *end;
      unsigned long combo;
      idx++;
      combo = strtoul(argv[idx], &end, 10);
      if (*end == '\0' && combo < 61440)
      {
        set_single_combo((unsigned int)combo);
      }
      idx++;
      continue;
    }
    else if (idx+1<argc && strcmp(argv[idx], "-partition-range")==0)
    {
      /* Parse START/STRIDE/TOTAL format for strided partition (0-indexed)
       * Example: -partition-range 0/64/61440 handles partitions 0,64,128,...
       * This allows distributing 61,440 partitions across 64 workers via striding
       */
      char *slash1, *slash2;
      idx++;
      slash1 = strchr(argv[idx], '/');
      if (slash1 != NULL)
      {
        slash2 = strchr(slash1 + 1, '/');
        if (slash2 != NULL)
        {
          char *end;
          unsigned long start, stride, total;
          start = strtoul(argv[idx], &end, 10);
          if (end == slash1)
          {
            stride = strtoul(slash1 + 1, &end, 10);
            if (end == slash2)
            {
              total = strtoul(slash2 + 1, &end, 10);
              if (*end == '\0' && stride > 0 && total > 0 && start < total)
              {
                set_partition_range((unsigned int)start, (unsigned int)stride, (unsigned int)total);
              }
            }
          }
        }
      }
      idx++;
      continue;
    }
    else
      break;
  }

  TraceFunctionExit(__func__);
  TraceFunctionResult("%d",idx);
  TraceFunctionResultEnd();
  return idx;
}

void command_line_options_parser_solve(slice_index si)
{
  TraceFunctionEntry(__func__);
  TraceFunctionParamListEnd();

  int const argc = SLICE_U(si).command_line_options_parser.argc;
  char **argv = SLICE_U(si).command_line_options_parser.argv;
  int const idx_end_of_options = parseCommandlineOptions(argc,argv);
  char const *filename = idx_end_of_options<argc ? argv[idx_end_of_options] : "";
  slice_index const opener = input_plaintext_alloc_opener(filename);

  slice_insertion_insert(si,&opener,1);

  pipe_solve_delegate(si);

  TraceFunctionExit(__func__);
  TraceFunctionResultEnd();
}

slice_index alloc_command_line_options_parser(int argc, char **argv)
{
  slice_index const result = alloc_pipe(STCommandLineOptionsParser);
  SLICE_U(result).command_line_options_parser.argc = argc;
  SLICE_U(result).command_line_options_parser.argv = argv;
  return result;
}
