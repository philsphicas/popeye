# Parallel Solving in Popeye

This document describes the parallel solving features in Popeye for advanced users who want to understand the technical details and optimize performance for hard problems.

## Overview

Popeye's intelligent mode solves helpmate problems in two phases:

1. **Target position generation**: Iterates through combinations of (king_square, checker_piece, check_square) to generate potential mating positions
2. **Forward solve**: For each target position, searches from the initial position to find a path that reaches it

The total number of combinations is 64 × 15 × 64 = **61,440 combos**. However, workload is extremely uneven - most combos are quickly pruned, while a few "heavy" combos may take hours to solve.

## Command-Line Options

### Basic Parallel Solving

#### `-parallel N`

Spawns N worker processes to solve in parallel using fork-based parallelism.

```bash
./py -parallel 64 problem.inp
```

**How it works:**
- Each worker handles a strided subset of the 61,440 combos
- Worker i (1-indexed) processes combos: i-1, i-1+N, i-1+2N, ...
- This striding distributes heavy combos across workers (vs. contiguous ranges which might cluster heavy combos on one worker)

**Recommended values:**
- Start with the number of CPU cores
- For hard problems, more workers (up to 128-192) can help if system limits allow

**Output:**
- Progress messages show aggregated positions across all workers
- Solutions are printed as they're found
- Status updates show which workers are still running

---

### Partition Control (Manual)

These options give fine-grained control over which combos a single Popeye instance processes. Useful for external orchestration or debugging.

#### `-partition N/M`

Process partition N of M (1-indexed).

```bash
./py -partition 1/4 problem.inp   # Process 25% of combos
./py -partition 2/4 problem.inp   # Process another 25%
```

**How it works:**
- Combo belongs to partition N if: `(combo_index % M) == (N - 1)`
- Simple modulo-based distribution

#### `-partition-range START/STRIDE/MAX`

Process combos using strided iteration (0-indexed).

```bash
./py -partition-range 0/64/61440 problem.inp   # Combos 0, 64, 128, ...
./py -partition-range 1/64/61440 problem.inp   # Combos 1, 65, 129, ...
```

**How it works:**
- Processes combo indices: START, START+STRIDE, START+2×STRIDE, ...
- Used internally by `-parallel` to distribute work

#### `-partition-order ORDER`

Controls which dimension varies fastest in combo enumeration.

```bash
./py -partition-order kpc problem.inp   # King varies fastest (default)
./py -partition-order cpk problem.inp   # Check-square varies fastest
```

**Valid orders:** Any permutation of "kpc" (king, piece/checker, check-square)

**Why it matters:**
- Different orders distribute heavy combos differently across workers
- If one order clusters heavy combos together, try another
- The `-probe` mode tests all orderings automatically

---

### First-Move Partitioning

#### `-first-move-partition N/M`

Partition the first move of the forward solve phase.

```bash
./py -first-move-partition 1/4 problem.inp   # Only explore 25% of first moves
./py -first-move-partition 2/4 problem.inp   # Explore another 25%
```

**How it works:**
- At ply 1 of the forward solve, filters the move list
- Worker N only explores moves where: `(move_index % M) == (N - 1)`
- Combined with combo partitioning, enables 2D parallelization

**Use case:**
When a single combo is extremely heavy, this allows subdividing the work further. Running 4 instances with `-first-move-partition 1/4` through `4/4` on the same combo divides the search space.

#### `-first-move-queue N`

Spawn N workers to dynamically pull first moves from a shared queue.

```bash
./py -first-move-queue 4 -single-combo 30212 problem.inp
```

**How it works:**
- Requires `-single-combo` (must specify which combo to parallelize)
- Workers dynamically pull first moves from a shared work queue
- Uses file locking for atomic queue access across processes
- Each worker processes moves until the queue is exhausted

**Compared to `-first-move-partition`:**
- **Partition**: Static assignment, simple but may have load imbalance
- **Queue**: Dynamic assignment, better load balancing but with overhead

**Performance characteristics:**
- 500ms startup delay to ensure all workers are ready before pulling
- 1ms yield after each move to improve distribution
- Works best when individual first moves take significant time (seconds+)
- For fast moves (instantly rejected), distribution may still be uneven

**Example:**
```bash
# Attack a heavy combo with 8 workers using dynamic work queue
./py -first-move-queue 8 -single-combo 30212 problem.inp
```

#### `-single-combo INDEX`

Process only a single combo by its index (0-indexed, 0-61439).

```bash
./py -single-combo 30212 problem.inp   # Only process combo 30212
```

**Use case:**
Targeting a known heavy combo for focused analysis or parallel subdivision. Can be combined with `-first-move-partition` or `-first-move-queue`.

```bash
# Parallelize a single heavy combo across 4 workers (static partition)
./py -single-combo 30212 -first-move-partition 1/4 problem.inp &
./py -single-combo 30212 -first-move-partition 2/4 problem.inp &
./py -single-combo 30212 -first-move-partition 3/4 problem.inp &
./py -single-combo 30212 -first-move-partition 4/4 problem.inp &

# Or using dynamic work queue
./py -first-move-queue 4 -single-combo 30212 problem.inp
```

---

### Dynamic Rebalancing

#### `-rebalance [TIMEOUT]`

Enable dynamic rebalancing of work when used with `-parallel`.

```bash
./py -parallel 64 -rebalance problem.inp        # Rebalance after 60s (default)
./py -parallel 64 -rebalance 120 problem.inp    # Rebalance after 120s
```

**How it works:**

1. **Initial phase**: All N workers start with strided combo partitioning
2. **After TIMEOUT seconds**: 
   - Identifies workers that have finished (free slots available)
   - Identifies workers still running and their current "heavy" combo
   - Spawns helper workers in free slots
3. **Helper workers**:
   - Target specific heavy combos using `-single-combo`
   - Use `-first-move-partition` to divide work within that combo
   - Work in parallel with original workers (which continue running)

**Example scenario:**
- 64 workers start solving
- After 60s: 60 workers finished, 4 still running on heavy combos
- Rebalancing spawns 60 helpers (15 per heavy combo with first-move partitioning)
- Heavy combos now have 16 workers each (1 original + 15 helpers)

**Trade-offs:**
- Adds overhead for spawning helpers
- May produce duplicate solutions (both original and helpers find same solution)
- Most effective for very hard problems with extreme workload imbalance
- For easier problems, the overhead may outweigh benefits

---

### Diagnostic Modes

#### `-probe [TIMEOUT]`

Cycle through all partition orders to identify heavy combos.

```bash
./py -parallel 64 -probe problem.inp        # 60s per order (default)
./py -parallel 64 -probe 30 problem.inp     # 30s per order
```

**How it works:**
- Tests each of the 6 partition orders (kpc, kcp, pkc, pck, ckp, cpk)
- For each order, runs workers for TIMEOUT seconds
- Records which combos didn't finish (heavy combos)
- Prints summary of heavy combos and their frequency

**Use case:**
Understanding the workload distribution before committing to a long solve.

#### `-worker`

Run in worker mode with structured output for subprocess coordination.

```bash
./py -worker problem.inp
```

**Output format:**
- `@@COMBO:N info` - Starting work on combo N
- `@@PROGRESS:M+K:positions` - Progress at depth M+K
- `@@TEXT:message` - Human-readable text (solutions, etc.)
- `@@FINISHED` - Worker completed

Used internally by `-parallel` for parent-child communication.

---

## Technical Details

### Combo Index Calculation

The combo index is calculated based on `-partition-order`:

```
For order "kpc" (king varies fastest):
  combo = check_sq × 960 + checker × 64 + king

For order "cpk" (check-square varies fastest):
  combo = king × 960 + checker × 64 + check_sq
```

Where:
- king: 0-63 (square index)
- checker: 0-14 (which white piece gives check)
- check_sq: 0-63 (square the check comes from)

### Worker Communication Protocol

Workers communicate with the parent via stdout using `@@` prefixed messages:

| Message | Description |
|---------|-------------|
| `@@COMBO:N king=SQ checker=P checksq=SQ` | Starting combo N |
| `@@PROGRESS:M+K:positions` | At depth M+K with N positions |
| `@@TEXT:text` | Pass-through text (solutions) |
| `@@FINISHED` | Worker done |
| `@@DEBUG:msg` | Debug message (suppressed) |

### Memory and Resource Limits

- Worker count capped at 1024
- Each worker is a full fork of the parent process
- Memory usage scales linearly with worker count
- File descriptor limits may restrict worker count on some systems

### Combining Options

Options can be combined for advanced use cases:

```bash
# External orchestration: 4 machines, each running 16 workers on 1/4 of combos
machine1$ ./py -parallel 16 -partition 1/4 problem.inp
machine2$ ./py -parallel 16 -partition 2/4 problem.inp
machine3$ ./py -parallel 16 -partition 3/4 problem.inp
machine4$ ./py -parallel 16 -partition 4/4 problem.inp

# Target heavy combo with maximum parallelism
./py -parallel 32 -single-combo 30212 problem.inp

# Note: -parallel with -single-combo uses first-move partitioning automatically
```

---

## Performance Tuning

### Identifying Heavy Combos

1. Run with `-probe` to get a list of heavy combos
2. Check status output during `-parallel` runs to see which workers are slow
3. Combo info is printed: `W5: 30212 king=e1 checker=Bh4 checksq=h4`

### Optimizing for Your Problem

| Problem Type | Recommended Approach |
|--------------|---------------------|
| Quick (<1 min) | `-parallel N` where N = CPU cores |
| Medium (1-10 min) | `-parallel N` with N = 2× CPU cores |
| Hard (>10 min) | `-parallel N -rebalance` |
| Very hard (hours) | Multiple runs with `-partition`, external orchestration |
| Single heavy combo | `-single-combo INDEX -first-move-partition N/M` |

### System Limits

Check your system's limits:

```bash
ulimit -u    # Max user processes
ulimit -n    # Max open files (need 2 per worker for pipes)
```

Increase if needed:
```bash
ulimit -u 4096
ulimit -n 4096
```

---

## Examples

### Basic parallel solve
```bash
./py -parallel 64 problem.inp
```

### Parallel with rebalancing for hard problems
```bash
./py -parallel 64 -rebalance 120 problem.inp
```

### Probe to understand workload distribution
```bash
./py -parallel 64 -probe 60 problem.inp
```

### Manual 2D parallelization of a heavy combo
```bash
# Identify heavy combo from previous run: 30212
# Split across 8 first-move partitions
for i in $(seq 1 8); do
  ./py -single-combo 30212 -first-move-partition $i/8 problem.inp &
done
wait
```

### Distributed solving across machines
```bash
# On each of 4 machines, run 64 workers on 1/4 of the problem
./py -parallel 64 -partition $MACHINE_ID/4 problem.inp
```

---

## Troubleshooting

### Workers not starting
- Check `ulimit -u` (max processes)
- Check `ulimit -n` (max file descriptors)
- Try fewer workers

### Uneven progress
- Some combos are inherently harder
- Try different `-partition-order`
- Use `-rebalance` for dynamic load balancing

### Missing solutions
- Ensure all partitions are covered
- With rebalancing, duplicates are possible but no solutions should be lost

### High memory usage
- Each worker is a full process fork
- Reduce worker count or use external orchestration with `-partition`
