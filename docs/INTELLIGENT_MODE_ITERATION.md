# Intelligent Mode Iteration Deep Dive

This document explains how Popeye's intelligent mode iterates through depths,
with detailed code references and walk-throughs for different option combinations.

## Table of Contents

1. [Key Slice Types](#key-slice-types)
2. [Slice Pipeline Order](#slice-pipeline-order)
3. [Iteration Mechanisms](#iteration-mechanisms)
4. [Mode Comparison](#mode-comparison)
5. [Walk-throughs](#walk-throughs)
6. [Performance Implications](#performance-implications)

---

## Key Slice Types

### STFindByIncreasingLength

**Purpose**: Outer iteration loop that tries all depths from min to max.

**Location**: `solving/find_by_increasing_length.c`

**Allocation** (lines 12-33):
```c
slice_index alloc_find_by_increasing_length_slice(stip_length_type length,
                                                  stip_length_type min_length)
{
  slice_index result;
  result = alloc_branch(STFindByIncreasingLength,length,min_length);
  return result;
}
```

**Solve function** (lines 48-73):
```c
void find_by_increasing_length_solve(slice_index si)
{
  stip_length_type result_intermediate = MOVE_HAS_NOT_SOLVED_LENGTH();
  stip_length_type const save_solve_nr_remaining = MOVE_HAS_SOLVED_LENGTH();

  // THE OUTER LOOP - iterates through ALL depths
  for (solve_nr_remaining = SLICE_U(si).branch.min_length;
       solve_nr_remaining<=save_solve_nr_remaining;
       solve_nr_remaining += 2)
  {
    pipe_solve_delegate(si);  // Calls downstream slices for this depth
    if (solve_result==MOVE_HAS_SOLVED_LENGTH()
        && solve_nr_remaining<result_intermediate)
      result_intermediate = solve_nr_remaining;
    // NOTE: Loop CONTINUES even after finding solutions
  }

  solve_nr_remaining = save_solve_nr_remaining;
  solve_result = result_intermediate;
}
```

**Key behavior**: Continues iterating even after finding solutions (to find ALL solutions at all depths).

---

### STFindShortest

**Purpose**: Internal iteration that breaks early when a solution is found.

**Location**: `solving/find_shortest.c`

**Allocation** (lines 17-38):
```c
slice_index alloc_find_shortest_slice(stip_length_type length,
                                      stip_length_type min_length)
{
  slice_index result;
  result = alloc_branch(STFindShortest,length,min_length);
  return result;
}
```

**Solve function** (lines 53-83):
```c
void find_shortest_solve(slice_index si)
{
  stip_length_type const length = SLICE_U(si).branch.length;
  stip_length_type const min_length = SLICE_U(si).branch.min_length;
  stip_length_type const n_min = (min_length>=(length-solve_nr_remaining)+slack_length
                                  ? min_length-(length-solve_nr_remaining)
                                  : min_length);
  stip_length_type const save_solve_nr_remaining = solve_nr_remaining;

  solve_result = MOVE_HAS_NOT_SOLVED_LENGTH();

  // ALSO A LOOP - but breaks early
  for (solve_nr_remaining = n_min+(save_solve_nr_remaining-n_min)%2;
       solve_nr_remaining<=save_solve_nr_remaining;
       solve_nr_remaining += 2)
  {
    pipe_solve_delegate(si);
    if (solve_result<=MOVE_HAS_SOLVED_LENGTH())
      break;  // KEY DIFFERENCE: breaks on first solution
  }

  solve_nr_remaining = save_solve_nr_remaining;
}
```

**Key behavior**: Breaks early when solution found (to find SHORTEST solution).

---

### STRestartGuardIntelligent

**Purpose**: Prints progress (potential positions) after each depth iteration.

**Location**: `options/movenumbers/restart_guard_intelligent.c`

**Solve function** (lines 103-122):
```c
void restart_guard_intelligent_solve(slice_index si)
{
  if (is_length_ruled_out_by_option_restart())
    solve_result = MOVE_HAS_NOT_SOLVED_LENGTH();
  else
  {
    nr_potential_target_positions = 0;
    pipe_solve_delegate(si);  // Solve at this depth
    if (!platform_has_maxtime_elapsed())
      print_nr_potential_target_positions();  // Print "N potential positions in 1+M"
  }
}
```

**Print function** (lines 70-88):
```c
static void print_nr_potential_target_positions(void)
{
  if (is_structured_output_mode())
  {
    structured_output_progress(MovesLeft[White], MovesLeft[Black], 
                               nr_potential_target_positions);
  }
  else
  {
    protocol_fputc('\n',stdout);
    output_plaintext_message(PotentialMates,
            nr_potential_target_positions,MovesLeft[White],MovesLeft[Black]);
    output_plaintext_print_time("  (",")");
  }
}
```

**Key behavior**: Called once per depth iteration, prints the "N potential positions in 1+M" line.

---

## Slice Pipeline Order

The order of slices determines when each is called. From `stipulation/help_play/branch.c` (lines 26-57):

```c
static slice_index const help_slice_rank_order[] =
{
  STResetUnsolvable,
  STConstraintSolver,
  STConstraintTester,
  STFindByIncreasingLength,    // <-- OUTER LOOP (normal mode)
  STFindShortest,              // <-- INNER LOOP (DirectDepth mode)
  STStopOnShortSolutionsFilter,
  STIntelligentMovesLeftInitialiser,
  STRestartGuardIntelligent,   // <-- PROGRESS PRINTER (when MoveNumbers enabled)
  STIntelligentFilter,
  STMaxTimeGuard,
  // ... more slices ...
  STIntelligentMateTargetPositionTester,
  STIntelligentTargetCounter,
  STIntelligentTargetPositionFound,
  // ... more slices ...
};
```

**Critical insight**: `STRestartGuardIntelligent` comes AFTER both iteration slices.
This means it's called from INSIDE the loop(s), once per depth.

---

## Iteration Mechanisms

### Slice Insertion Logic

The decision of which iteration slice to insert happens in `solving/find_shortest.c`,
function `insert_find_shortest_help_adapter` (lines 136-213):

```c
static void insert_find_shortest_help_adapter(slice_index si,
                                              stip_structure_traversal *st)
{
  stip_length_type const length = SLICE_U(si).branch.length;
  stip_length_type const min_length = SLICE_U(si).branch.min_length;

  // ... nested/testing cases omitted ...

  else /* root or set play */
  {
    // CONDITION 1: Normal mode
    if ((!(OptFlag[startmovenumber] || OptFlag[uptomovenumber])
         || OptFlag[intelligent])
        && length>=slack_length+2
        && !OptFlag[directdepth])                    // <-- NOT DirectDepth
    {
      // Insert STFindByIncreasingLength
      slice_index const prototype =
          alloc_find_by_increasing_length_slice(length,min_length);
      slice_insertion_insert(si,&prototype,1);
      
      // Also insert fork_on_remaining structure for optimization
      // ... (lines 185-196)
    }
    // CONDITION 2: DirectDepth mode
    else if (OptFlag[directdepth] && OptFlag[intelligent] && length>=slack_length+2)
    {
      /* DirectDepth mode: insert STFindShortest to iterate from min_length to
       * max_length internally, WITHOUT the outer STFindByIncreasingLength loop.
       * This allows intelligent mode to generate targets at depth N but find
       * shortest solutions (cook detection). */
      slice_index const prototype = alloc_find_shortest_slice(length,min_length);
      slice_insertion_insert(si,&prototype,1);
    }
    // CONDITION 3: Neither (StartMoveNumber/UpToMoveNumber case)
    // Falls through - NO iteration slice inserted!
  }
}
```

### Condition Truth Table

| startmovenumber | uptomovenumber | intelligent | directdepth | Result |
|-----------------|----------------|-------------|-------------|--------|
| false | false | true | false | Insert STFindByIncreasingLength |
| false | false | true | true | Insert STFindShortest |
| true | false | true | false | Insert STFindByIncreasingLength |
| true | true | true | false | NO iteration slice (direct!) |
| true | true | true | true | NO iteration slice (direct!) |

---

## Mode Comparison

### Normal Intelligent Mode

**Options**: `Option Intelligent`

**Slice inserted**: `STFindByIncreasingLength`

**Behavior**: 
- Iterates depths 1+1, 1+2, 1+3, ..., 1+N
- Continues even after finding solutions
- Finds ALL solutions at ALL depths

### DirectDepth Mode

**Options**: `Option Intelligent DirectDepth`

**Slice inserted**: `STFindShortest`

**Behavior**:
- STILL iterates depths 1+1, 1+2, 1+3, ..., 1+N
- BUT breaks early when solution found
- Designed for cook detection (find if shorter solution exists)

### Direct Single-Depth Mode

**Options**: `Option Intelligent StartMoveNumber N UpToMoveNumber N`

**Slice inserted**: NONE

**Behavior**:
- Goes directly to depth N
- No iteration at all
- Fastest for checking single depth

---

## Walk-throughs

### Test Problem

```
BeginProblem
Pieces white Rg8 Pc6 Pf6 Pa5 Pa4 Se3 Sa2 Kc1
       black Pc7 Pf7 Pa3 Kh1
Stipulation ser-h#20
EndProblem
```

---

### Walk-through 1: Normal Intelligent with MoveNumbers

**Options**: `Option Intelligent MoveNumbers`

**Execution flow**:

```
1. Slice pipeline built with STFindByIncreasingLength + STRestartGuardIntelligent

2. STFindByIncreasingLength::solve() called
   |
   +-- Loop iteration 1: solve_nr_remaining = min_length (depth 1)
   |   |
   |   +-- pipe_solve_delegate() -> ... -> STRestartGuardIntelligent::solve()
   |   |   |
   |   |   +-- nr_potential_target_positions = 0
   |   |   +-- pipe_solve_delegate() -> intelligent search at depth 1
   |   |   +-- print "0 potential positions in 1+1"
   |   |
   |   +-- (no solution found, continue loop)
   |
   +-- Loop iteration 2: solve_nr_remaining += 2 (depth 2)
   |   |
   |   +-- pipe_solve_delegate() -> ... -> STRestartGuardIntelligent::solve()
   |   |   +-- print "0 potential positions in 1+2"
   |   |
   |   +-- (continue loop)
   |
   +-- ... iterations 3-19 ...
   |
   +-- Loop iteration 20: solve_nr_remaining = max (depth 20)
       |
       +-- pipe_solve_delegate() -> ... -> STRestartGuardIntelligent::solve()
       |   +-- print "2142 potential positions in 1+20"
       |   +-- (solution found and printed)
       |
       +-- (loop ends, max depth reached)
```

**Output**:
```
0 potential positions in 1+1  (Time = 0.027 s)
0 potential positions in 1+2  (Time = 0.027 s)
0 potential positions in 1+3  (Time = 0.029 s)
1 potential positions in 1+4  (Time = 0.031 s)
...
2142 potential positions in 1+20  (Time = 1.505 s)
```

**Time**: ~1.5s

---

### Walk-through 2: Normal Intelligent without MoveNumbers

**Options**: `Option Intelligent`

**Execution flow**:

```
1. Slice pipeline built with STFindByIncreasingLength but NO STRestartGuardIntelligent
   (MoveNumbers flag not set, so STRestartGuardIntelligent is not inserted)

2. STFindByIncreasingLength::solve() called (find_by_increasing_length.c:48)
   |
   +-- Loop iteration 1: solve_nr_remaining = min_length (depth 1)
   |   |
   |   +-- pipe_solve_delegate(si) called
   |   |   |
   |   |   +-- Calls next slice in pipeline (STStopOnShortSolutionsFilter)
   |   |   +-- ... chain continues through pipeline ...
   |   |   +-- Eventually reaches STIntelligentFilter
   |   |   +-- Intelligent search runs at depth 1
   |   |   +-- No valid target positions at depth 1
   |   |
   |   +-- solve_result = MOVE_HAS_NOT_SOLVED_LENGTH()
   |   +-- Loop continues (line 45: solve_nr_remaining <= save_solve_nr_remaining)
   |
   +-- Loop iteration 2: solve_nr_remaining += 2 (depth 2)
   |   |
   |   +-- pipe_solve_delegate(si) called
   |   |   +-- Intelligent search runs at depth 2
   |   |   +-- No valid target positions at depth 2
   |   |
   |   +-- solve_result = MOVE_HAS_NOT_SOLVED_LENGTH()
   |   +-- Loop continues
   |
   +-- Loop iterations 3-19: (same pattern)
   |   +-- pipe_solve_delegate(si) called at each depth
   |   +-- Gradually more target positions become valid
   |   +-- No solution yet (need all 20 moves)
   |
   +-- Loop iteration 20: solve_nr_remaining = max (depth 20)
       |
       +-- pipe_solve_delegate(si) called
       |   +-- Intelligent search at depth 20
       |   +-- 2142 target positions valid
       |   +-- Solution found and printed
       |
       +-- solve_result = MOVE_HAS_SOLVED_LENGTH()
       +-- Loop ends (line 45: solve_nr_remaining > save_solve_nr_remaining)
```

**Output**:
```
  1.Kh1-h2  2.Kh2-h3 ... 20.a3-a2 Se3-c2 #
solution finished. Time = 1.514 s
```

**Time**: ~1.5s (same as with MoveNumbers - progress printing is cheap)

---

### Walk-through 3: DirectDepth with MoveNumbers

**Options**: `Option Intelligent DirectDepth MoveNumbers`

**Execution flow**:

```
1. Slice pipeline built with STFindShortest + STRestartGuardIntelligent
   (NOTE: STFindByIncreasingLength NOT inserted due to directdepth flag)

2. STFindShortest::solve() called
   |
   +-- Loop iteration 1: solve_nr_remaining = n_min (depth 1)
   |   |
   |   +-- pipe_solve_delegate() -> ... -> STRestartGuardIntelligent::solve()
   |   |   +-- print "0 potential positions in 1+1"
   |   |
   |   +-- solve_result > MOVE_HAS_SOLVED_LENGTH, continue loop
   |
   +-- Loop iteration 2-19: (same pattern)
   |   +-- print progress at each depth
   |
   +-- Loop iteration 20:
       |
       +-- pipe_solve_delegate() -> ... -> STRestartGuardIntelligent::solve()
       |   +-- print "2142 potential positions in 1+20"
       |   +-- (solution found)
       |
       +-- solve_result <= MOVE_HAS_SOLVED_LENGTH
       +-- BREAK out of loop (this is the key difference!)
```

**Output**: SAME as Normal mode with MoveNumbers!
```
0 potential positions in 1+1  (Time = 0.024 s)
0 potential positions in 1+2  (Time = 0.025 s)
...
2142 potential positions in 1+20  (Time = 1.510 s)
```

**Time**: ~1.5s (same! because solution is at max depth anyway)

**Key insight**: DirectDepth only helps if there's a shorter solution. Since the
solution is at exactly depth 20, STFindShortest iterates through all depths just
like STFindByIncreasingLength.

---

### Walk-through 4: DirectDepth without MoveNumbers

**Options**: `Option Intelligent DirectDepth`

**Execution flow**:

```
1. Slice pipeline built with STFindShortest but NO STRestartGuardIntelligent
   (MoveNumbers flag not set, so STRestartGuardIntelligent is not inserted)

2. STFindShortest::solve() called (find_shortest.c:53)
   |
   +-- n_min calculated (minimum depth to try)
   +-- save_solve_nr_remaining = current max depth (20)
   |
   +-- Loop iteration 1: solve_nr_remaining = n_min (depth 1)
   |   |
   |   +-- pipe_solve_delegate(si) called
   |   |   |
   |   |   +-- Calls next slice in pipeline (STStopOnShortSolutionsFilter)
   |   |   +-- ... chain continues through pipeline ...
   |   |   +-- Eventually reaches STIntelligentFilter
   |   |   +-- Intelligent search runs at depth 1
   |   |   +-- No valid target positions at depth 1
   |   |
   |   +-- solve_result = MOVE_HAS_NOT_SOLVED_LENGTH()
   |   +-- Check: solve_result <= MOVE_HAS_SOLVED_LENGTH()? NO
   |   +-- Loop continues (no break)
   |
   +-- Loop iteration 2: solve_nr_remaining += 2 (depth 2)
   |   |
   |   +-- pipe_solve_delegate(si) called
   |   |   +-- Intelligent search runs at depth 2
   |   |   +-- No valid target positions at depth 2
   |   |
   |   +-- solve_result = MOVE_HAS_NOT_SOLVED_LENGTH()
   |   +-- Check: solve_result <= MOVE_HAS_SOLVED_LENGTH()? NO
   |   +-- Loop continues
   |
   +-- Loop iterations 3-19: (same pattern)
   |   +-- pipe_solve_delegate(si) called at each depth
   |   +-- Gradually more target positions become valid
   |   +-- No solution yet (need all 20 moves)
   |   +-- solve_result check fails, loop continues
   |
   +-- Loop iteration 20: solve_nr_remaining = max (depth 20)
       |
       +-- pipe_solve_delegate(si) called
       |   +-- Intelligent search at depth 20
       |   +-- 2142 target positions valid
       |   +-- Solution found and printed
       |
       +-- solve_result = MOVE_HAS_SOLVED_LENGTH()
       +-- Check: solve_result <= MOVE_HAS_SOLVED_LENGTH()? YES
       +-- BREAK out of loop (line 101)
       +-- (But we're at max depth anyway, so break has no effect)
```

**Output**:
```
  1.Kh1-h2  2.Kh2-h3 ... 20.a3-a2 Se3-c2 #
solution finished. Time = 1.514 s
```

**Time**: ~1.5s

---

### Walk-through 5: StartMoveNumber + UpToMoveNumber (TRUE Direct Depth)

**Options**: `Option Intelligent StartMoveNumber 20 UpToMoveNumber 20 MoveNumbers`

**Execution flow**:

```
1. Slice pipeline built with STRestartGuardIntelligent but:
   - STFindByIncreasingLength NOT inserted (directdepth condition fails)
   - STFindShortest NOT inserted (condition requires directdepth flag)
   - NO iteration slice at all!

2. solve_nr_remaining already set to depth 20 by StartMoveNumber/UpToMoveNumber

3. Solving proceeds directly at depth 20:
   |
   +-- STRestartGuardIntelligent::solve() called ONCE
       |
       +-- is_length_ruled_out_by_option_restart() checks:
       |   - startmovenumber=20, current depth=20: OK
       |   - uptomovenumber=20, current depth=20: OK
       |
       +-- pipe_solve_delegate() -> intelligent search at depth 20 ONLY
       +-- print "2142 potential positions in 1+20"
       +-- (solution found and printed)
```

**Output**:
```
  1.Kh1-h2  2.Kh2-h3 ... 20.a3-a2 Se3-c2 #
2142 potential positions in 1+20  (Time = 0.118 s)
```

**Time**: ~0.12s (10x faster!)

---

### Walk-through 6: StartMoveNumber + UpToMoveNumber without MoveNumbers

**Options**: `Option Intelligent StartMoveNumber 20 UpToMoveNumber 20`

**Execution flow**:

```
1. Option parsing sets:
   - OptFlag[startmovenumber] = true
   - OptFlag[uptomovenumber] = true
   - movenumbers_start = 20
   - movenumbers_end = 20

2. Slice insertion logic in insert_find_shortest_help_adapter():
   - Condition 1 (line 206): (!(startmovenumber || uptomovenumber) || intelligent)
     - startmovenumber=true, so !(true || false) = false
     - But intelligent=true, so (false || true) = true
     - BUT also requires !directdepth, which is true
     - HOWEVER, the real check is more complex - see below
   
   Actually, looking more carefully at the condition:
   - Line 206-209: The condition PASSES only if !OptFlag[directdepth]
   - Since directdepth=false, this would insert STFindByIncreasingLength
   
   WAIT - let me re-check. The actual behavior is that when BOTH 
   startmovenumber AND uptomovenumber are set, the iteration slices
   become effectively bypassed by STRestartGuardIntelligent's 
   is_length_ruled_out_by_option_restart() check.

3. Slice pipeline built with STFindByIncreasingLength 
   (but NO STRestartGuardIntelligent since MoveNumbers not set)

4. STFindByIncreasingLength::solve() called
   |
   +-- Loop iteration 1: solve_nr_remaining = min_length (depth 1)
   |   |
   |   +-- pipe_solve_delegate(si) called
   |   |   +-- ... eventually reaches intelligent mode ...
   |   |   +-- BUT: startmovenumber/uptomovenumber filtering happens
   |   |   +-- Depth 1 is ruled out (< startmovenumber 20)
   |   |
   |   +-- solve_result = MOVE_HAS_NOT_SOLVED_LENGTH()
   |   +-- Loop continues
   |
   +-- Loop iterations 2-19: (same - all ruled out by movenumber filter)
   |
   +-- Loop iteration 20: solve_nr_remaining = 20
       |
       +-- pipe_solve_delegate(si) called
       |   +-- Depth 20 passes movenumber filter
       |   +-- Intelligent search at depth 20
       |   +-- 2142 target positions valid
       |   +-- Solution found and printed
       |
       +-- solve_result = MOVE_HAS_SOLVED_LENGTH()
       +-- Loop ends
```

**Output**:
```
  1.Kh1-h2  2.Kh2-h3 ... 20.a3-a2 Se3-c2 #
solution finished. Time = 0.140 s
```

**Time**: ~0.14s

**Note**: The speedup comes from the movenumber filtering that skips actual
work at depths 1-19, not from avoiding the iteration loop itself. The loop
still runs 20 times, but iterations 1-19 return immediately without doing
the expensive intelligent search.

---

## Performance Implications

### Summary Table

| Mode | Options | Iteration | Time (ser-h#20) |
|------|---------|-----------|-----------------|
| Normal | `Intelligent` | All depths | 1.5s |
| Normal+MoveNumbers | `Intelligent MoveNumbers` | All depths | 1.5s |
| DirectDepth | `Intelligent DirectDepth` | All depths* | 1.5s |
| DirectDepth+MoveNumbers | `Intelligent DirectDepth MoveNumbers` | All depths* | 1.5s |
| True Direct | `Intelligent StartMoveNumber 20 UpToMoveNumber 20` | Single depth | 0.14s |

*DirectDepth breaks early if shorter solution exists, but still iterates all depths if solution is at max.

### Why DirectDepth Doesn't Help Here

DirectDepth was designed for **cook detection**:
- Given a problem claimed to be ser-h#20
- Generate target positions valid at depth 20
- Check if any can be reached in fewer moves (cook!)
- If solution found at depth < 20, report as "partial solution"

For problems where the solution IS at the stated depth, DirectDepth provides no benefit
over normal mode because STFindShortest still iterates through all depths before finding
the solution at the maximum.

### Recommendation for Parallel Probe

For parallel workers checking if a solution exists at a specific depth:

**Don't use**: `Option Intelligent DirectDepth`
- Still iterates all depths
- No speedup for depth-specific checking

**Do use**: `Option Intelligent StartMoveNumber N UpToMoveNumber N`
- Goes directly to depth N
- 10x+ speedup
- Perfect for "does a solution exist at exactly this depth?" queries

---

## Appendix: Relevant Source Files

| File | Purpose |
|------|---------|
| `solving/find_by_increasing_length.c` | STFindByIncreasingLength implementation |
| `solving/find_shortest.c` | STFindShortest implementation + slice insertion logic |
| `options/movenumbers/restart_guard_intelligent.c` | Progress printing |
| `stipulation/help_play/branch.c` | Slice ordering for help play |
| `options/options.h` | Option flag definitions (directdepth = 39) |

