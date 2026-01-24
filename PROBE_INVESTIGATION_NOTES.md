# DirectDepth Probe Investigation Notes

## Date: Jan 22, 2026

## Goal
Understand how quickly mating squares can be eliminated during DirectDepth parallel solving,
to find good heuristics for "early stopping" during probe phases.

## Data Collection

### Setup
- Script: `collect_raw_probe.py`
- Runs each problem with `Option Intelligent DirectDepth` and `-parallel 64`
- Captures raw Popeye output including worker completion timestamps
- 300s timeout, target ≤8 survivors

### Sample: 27 problems
- Depths: 20, 21, 30, 31, 40, 41, 42, 50, 51, 60, 62, 70, 71
- Results: 20 reached target, 7 timed out

## Key Findings

### 1. Hardness Metric (Initial)

Best predictor found:
```
hardness = depth × max(white_majors, 1) × (black_pieces² + 1)
```

| Hardness Score | Outcome |
|----------------|---------|
| ≤ 2600 | All reached target |
| ≥ 3500 | All timed out |

### 2. Why This Works (Initial Theory)

- **Depth**: More moves = exponentially more search space
- **White majors (Q/R/B/S)**: More mating potential than pawns, more combinations to explore
- **Black pieces squared**: Defensive resources - blockers, interpositions, escapes

### 3. Caveat: Pawn Promotion Factor

User insight: Pawns can provide MORE variability than majors at longer depths because:
- Each pawn can promote to Q/R/B/S
- Multiple pawns = combinatorial explosion of promotion choices
- This factor increases with depth (pawns need time to promote)

**TODO**: Refine metric to account for pawn promotion potential at higher depths.
Possible refinement:
```
pawn_factor = white_pawns × min(depth / 20, 4)  # caps at 4x for very long problems
```

### 4. Interesting Edge Cases

#### Problem 11 vs 12 (Same depth, same total pieces)
| ID | Stip | White | Black | Hardness | Outcome |
|----|------|-------|-------|----------|---------|
| 11 | ser-h#40 | 8 pawns | 8 pieces | 2600 | OK (1.6s) |
| 12 | ser-h#40 | R+2B+S+8P | 5 pieces | 4160 | TIMEOUT (64 survivors!) |

Problem 12 couldn't eliminate a SINGLE square in 5 minutes with 64 workers.

#### Problem 16 (partial progress)
- ser-h#50, 16W+3B (including Q, 2R, 2S, 2B)
- Started eliminating squares at 233s
- Got to 56 survivors (8 eliminated) before 300s timeout
- Shows elimination CAN happen, just very slowly

### 5. Elimination Curves

From Problem 16 raw output:
```
[233s] Worker 55 finished (63 remaining)
[235s] Worker 47 finished (62 remaining)
[238s] Worker 2 finished (61 remaining)
[266s] Workers 40, 51 finished (59 remaining)
[283s] Worker 39 finished (58 remaining)
[286s] Worker 10 finished (57 remaining)
[288s] Worker 48 finished (56 remaining)
```

**Pattern**: Once elimination starts, it tends to continue (workers finishing cluster).
This suggests an adaptive approach could work:
- If no progress in X seconds, problem is "hard"
- If progress starts, it may accelerate

### 6. Open Questions

1. **Does elimination stabilize?**
   - Do the "easy" squares get eliminated first, then plateau?
   - Or does elimination accelerate as constraints tighten?

2. **Would more workers help hard problems?**
   - Problem 12 with 192 workers instead of 64?
   - More partitioning = smaller search per worker

3. **Would more time help?**
   - Problem 12 with 10 min or 30 min timeout?
   - Is it just slow, or fundamentally stuck?

4. **Adaptive timeout strategy?**
   - Short initial probe (30s)
   - If progress, extend
   - If no progress, fall back to different strategy

## Next Steps

1. [ ] Test Problem 12 with longer timeout (10+ min)
2. [ ] Test Problem 12 with more partitions (192 workers)
3. [ ] Analyze elimination curves for more problems
4. [ ] Refine hardness metric with pawn promotion factor
5. [ ] Compare Direct vs non-Direct mode (baseline validation)

## Raw Data Location
- `/tmp/probe_data/varied_run1/` - 27 problem outputs + index.json

## Direct vs Normal Mode Comparison (No Parallelism)

### Test Results

| Problem | Stipulation | Pieces | Normal | Direct | Delta |
|---------|-------------|--------|--------|--------|-------|
| 14 | ser-h#41 | 10+2 | 2:10 | 2:13 | +1.4% |
| 21 | ser-h#60 | 13+1 | 39.0s | 42.9s | +10% |

### Conclusion

**Direct mode is slightly SLOWER without parallelism** (1-10% overhead)

This makes sense because Direct mode:
1. Does extra work checking goal squares (all 64 potential mating squares)
2. Has overhead from partitioning logic
3. The benefit of Direct comes from parallelism + early termination when squares are eliminated

Without parallelism, you're doing the same search PLUS extra bookkeeping.

### Implication for Strategy

- **Don't use Direct mode for single-threaded solving**
- **Direct mode only makes sense with parallelism**
- The break-even point is probably around 2+ workers, but real benefits come with more (8+)

## DirectDepth vs StartMoveNumber/UpToMoveNumber (Important Discovery!)

### Key Finding

DirectDepth mode does NOT skip iterative deepening! It still iterates through all depths via `STFindShortest`.

To truly solve at a single depth without iteration, use:
```
Option Intelligent StartMoveNumber N UpToMoveNumber N
```

### Performance Comparison (ser-h#20)

| Mode | Time | Potential Positions at 1+20 |
|------|------|----------------------------|
| Normal (iterative 1+1 to 1+20) | 1.505s | 2142 |
| DirectDepth | ~1.5s | Same iteration as normal |
| StartMoveNumber 20 UpToMoveNumber 20 | 0.118s | 2142 |

**10x speedup** by skipping depths 1-19!

### Why DirectDepth Iterates

DirectDepth was designed for **cook detection**:
- Generate target positions at depth N
- But verify if there's a shorter solution at M < N
- Uses `STFindShortest` which breaks early on first solution

For **parallel probe** use case (just checking existence at depth N):
- Use `StartMoveNumber N UpToMoveNumber N` instead
- This skips ALL iteration

### Implementation Details

Looking at `solving/find_shortest.c`:

1. **Normal mode**: Inserts `STFindByIncreasingLength` (iterates all depths, continues after solutions)

2. **DirectDepth mode**: Inserts `STFindShortest` (iterates all depths, breaks early on solution)

3. **StartMoveNumber + UpToMoveNumber**: Falls through both conditions (line 177: `!OptFlag[directdepth]` AND line 199: `OptFlag[directdepth] && OptFlag[intelligent]`), so NO iteration slice is inserted

### Recommendation

For parallel probe workers targeting specific depth:
- Consider using `-startmove N -uptomove N` instead of DirectDepth
- This would give true single-depth solving
- Need to verify this works with partition/worker mode
