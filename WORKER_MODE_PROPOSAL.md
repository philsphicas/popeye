# Popeye Worker Mode Proposal

## Overview

A standardized `-worker` mode that enables Popeye to be used as a subprocess
by external coordinators (like spinach.tcl) or internal parallelism, with
structured output that's easy to parse.

## Command Line

```
popeye -worker [other-options] [inputfile]
```

## Worker Mode Behaviors

When `-worker` is specified:

1. **Suppress greeting line** - no version banner
2. **Suppress board diagram** - equivalent to `option NoBoard`  
3. **Emit structured output protocol** - all output wrapped in parseable markers
4. **Heartbeats on stderr** - periodic `@@HEARTBEAT:` messages (replaces `-heartbeat`)
5. **Ready signal** - emit `@@READY` when ready to receive input

## Structured Output Protocol

All protocol lines start with `@@` and are emitted on stdout.

### Lifecycle Messages

```
@@READY                     # Worker initialized, ready for input
@@SOLVING                   # Started solving  
@@FINISHED                  # Done solving (normal completion)
@@PARTIAL                   # Done solving (hit maxsol or other limit)
@@ERROR:<message>           # Error occurred
```

### Multi-Problem Messages

Input files can contain multiple problems separated by `nextproblem`. Each problem
is bracketed with start/end markers:

```
@@PROBLEM_START:<index>     # Problem N starting (1-based)
@@PROBLEM_END:<index>       # Problem N finished
```

Example with two problems:
```
@@READY
@@PROBLEM_START:1
@@SOLVING
@@SOLUTION_START
@@TEXT:  1.Ke8-f7 Rh1-h7 #
@@SOLUTION_END
@@TIME:0.023
@@FINISHED
@@PROBLEM_END:1
@@PROBLEM_START:2
@@SOLVING
@@SOLUTION_START
@@TEXT:  1.Ka8-b8 Ra1-a8 #
@@SOLUTION_END
@@TIME:0.018
@@FINISHED
@@PROBLEM_END:2
```

### Solution Structure Messages

Chess problem solutions have hierarchical structure:
- A problem may have multiple **phases** (set play, actual play, etc.)
- Each phase has **solutions** (key moves or opening moves)
- Solutions contain **tries** (moves that almost work but have refutations)
- Solutions/tries contain **variations** (lines of play)
- Variations contain **threats**, **defenses**, **refutations**

```
@@PHASE_START:<type>        # setplay, actualplay, twinA, etc.
@@PHASE_END

@@SOLUTION_START            # Beginning of a solution/key
@@SOLUTION_END

@@TRY_START                 # Beginning of a try (refuted attempt)
@@TRY_END

@@VARIATION_START           # Beginning of a variation
@@VARIATION_END

@@THREAT:<moves>            # Threat line
@@DEFENSE:<move>            # Defense move
@@REFUTATION:<move>         # Move that refutes a try

@@MOVE:<notation>           # A move in standard notation
@@TEXT:<line>               # Raw text line (for compatibility)
```

### Move Annotations

```
@@KEY                       # This move is the key (!)
@@TRY                       # This move is a try (?)
@@CHECK                     # Move gives check (+)
@@MATE                      # Move is checkmate (#)
@@STALEMATE                 # Move is stalemate (=)
@@ZUGZWANG                  # Position is zugzwang
```

### Simpler Alternative: Line-Based Protocol

For simpler parsing (and spinach.tcl compatibility), we could use a flatter protocol
where each complete line of output is tagged:

```
@@LINE:<full line of output>
```

This wraps ALL stdout output, making parsing trivial:
- Lines starting with `@@` are protocol
- Everything else is wrapped in `@@LINE:`

Example:
```
@@SOLVING
@@LINE:  1.Ke8-f7 ? 
@@LINE:     1...Ra1-a8 !
@@LINE:
@@LINE:  1.Ke8-e7 !
@@LINE:     1...Ra1-a8
@@LINE:        2.Ra8-a1 #
@@FINISHED
```

### Progress Messages (stderr)

```
@@HEARTBEAT:<seconds>       # Periodic heartbeat with elapsed time
@@PROGRESS:<M>+<K>:<positions>  # Depth M+K completed, N positions examined  
@@STATUS:<text>             # Human-readable status (e.g., "searching a1")
```

### Timing Messages

```
@@TIME:<seconds>            # Total solving time
```

## Partition Options (work with -worker)

These options tell the worker which subset of the search space to explore:

### For Intelligent Mode (partition by target position)

```
-king-partition N/M         # Handle mating squares where (index % M) == N-1
-checker-partition N/M      # Handle checking configurations (index % M) == N-1
```

### For Regular Solving (partition by move - already exists!)

```
option StartMoveNumber N    # Start from move N  
option UpToMoveNumber M     # Stop at move M
```

This is what spinach.tcl already uses.

## Coordinator → Worker Communication

The worker may need input from the coordinator:

```
@@STOP                      # Coordinator tells worker to stop (maxsol reached globally)
@@PING                      # Coordinator checking if worker is alive
@@PONG                      # Worker response to PING
```

Workers read these from stdin. This allows the coordinator to:
1. Stop workers early when global maxsol is reached
2. Verify workers haven't hung

## Implementation Plan

### Phase 1: Basic Worker Mode

1. Add `-worker` flag to `input/commandline.c`
2. Add `platform/worker.h` and `platform/worker.c`:
   ```c
   void set_worker_mode(boolean enabled);
   boolean is_worker_mode(void);
   void worker_emit_ready(void);
   void worker_emit_solving(void);
   void worker_emit_solution_start(void);
   void worker_emit_solution_text(char const *line);
   void worker_emit_solution_end(void);
   void worker_emit_finished(boolean partial);
   void worker_emit_heartbeat(void);
   void worker_emit_progress(unsigned m, unsigned k, unsigned long positions);
   ```

3. Modify output code to call worker_emit_* functions when in worker mode

4. Integrate with existing `-heartbeat` mechanism

### Phase 2: Refactor Internal Parallelism

1. Change `platform/parallel.c` to spawn workers with `-worker` flag
2. Parse the structured protocol instead of current ad-hoc parsing
3. Remove the fork-after-setup complexity (optional - could keep for efficiency)

### Phase 3: Update spinach.tcl (optional, for maintainer)

The maintainer could update spinach.tcl to:
1. Use `-worker` instead of `-heartbeat`
2. Parse the structured protocol
3. Benefit from any new partition options

## Compatibility

- `-worker` is purely additive - doesn't break existing behavior
- spinach.tcl continues to work unchanged
- Internal `-parallel` can migrate incrementally

## Example Session

### Single Problem

Coordinator spawns:
```
popeye -worker -king-partition 1/4 problem.inp
```

Worker output:
```
@@READY
@@PROBLEM_START:1
@@SOLVING
@@SOLUTION_START
@@TEXT:  1.Ke8-f7 Rh1-h7 +   2.Kf7-e8 Ra1-a8 #
@@SOLUTION_END
@@SOLUTION_START
@@TEXT:  1.Ke8-e7 Rh1-h7 +   2.Ke7-e8 Ra1-a8 #
@@SOLUTION_END
@@TIME:0.045
@@FINISHED
@@PROBLEM_END:1
```

Stderr (if heartbeat enabled):
```
@@HEARTBEAT:1
@@PROGRESS:2+0:1523
@@HEARTBEAT:2
@@PROGRESS:2+1:4892
```

### Multiple Problems

Input file with `nextproblem`:
```
beginproblem
pieces white Ke1 Qd1 Ra1 Rh1 black Ke8
stip h#2
option intelligent

nextproblem
pieces white Ka1 Qb1 black Kc3
stip h#1
option intelligent
endproblem
```

Worker output:
```
@@READY
@@PROBLEM_START:1
@@SOLVING
@@SOLUTION_START
@@TEXT:  1.Ke8-f7 Rh1-h7 +   2.Kf7-e8 Ra1-a8 #
@@SOLUTION_END
@@TIME:0.037
@@FINISHED
@@PROBLEM_END:1
@@PROBLEM_START:2
@@SOLVING
@@SOLUTION_START
@@TEXT:  1.Kc3-d3 Qb1-b3 #
@@SOLUTION_END
@@TIME:0.018
@@FINISHED
@@PROBLEM_END:2
```

## Benefits

1. **Single protocol** for all parallelization approaches
2. **Easy to parse** from any language (TCL, Python, shell, C)
3. **Backward compatible** - existing tools keep working
4. **Extensible** - new message types can be added
5. **Debuggable** - structured output is easier to log/inspect
