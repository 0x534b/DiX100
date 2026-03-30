# `benchmark_indir_ld_rep`

This benchmark evaluates three ways of executing repeated indirect loads:

- `BASE`: scalar CPU pointer-chasing loop
- `MAA_INDIR_LD_REP`: fused `maa_indirect_load_rep` instruction path
- `MAA_LOOP`: looped single-hop `maa_indirect_load` path used as an apples-to-apples MAA baseline

At a high level, each lane starts from `starts[i]`, follows a pointer chain in `backing`, and repeats that dereference `reps[i]` times. The benchmark can compare the fused repeated-indirect-load instruction both against the CPU implementation and against repeated single-hop MAA loads.

## What The Benchmark Is Measuring

The benchmark is meant to answer:

- how performance changes as the number of active lanes `n` increases
- how performance changes as indirect-chain depth increases
- how `maa_indirect_load_rep` compares against the scalar CPU implementation
- how `maa_indirect_load_rep` compares against a looped single-hop MAA implementation
- whether behavior changes under regular, irregular, shallow, deep, and mixed-depth access patterns

The main knobs are:

- `n`: number of active lanes
- `depth`: maximum pointer-chain depth
- `scenario`: access pattern used to populate `starts`, `reps`, and `backing`
- `arena_mult`: working-set expansion factor for scenarios that randomize addresses

## Command-Line Interface

The benchmark binary accepts:

```bash
./benchmark_indir_ld_rep.o <n> <depth> <BASE|MAA_INDIR_LD_REP|MAA_LOOP|CMP> <scenario> [arena_mult]
```

Example:

```bash
./benchmark_indir_ld_rep.o 64 8 CMP bfs_adj_list 64
```

Meaning:

- `64` active lanes
- maximum depth `8`
- run all implementations and compare outputs
- use the `bfs_adj_list` scenario
- use `arena_mult=64`

## Scenarios

The benchmark currently supports six scenarios.

### `uniform_local`

Each lane gets a compact, contiguous pointer chain laid out locally in memory.

What it tests:

- best-case spatial locality
- minimal address randomness
- whether the fused instruction still helps when the CPU baseline already has a cache-friendly layout

Interpretation:

- this is the easiest case for caches and prefetching
- if `MAA_INDIR_LD_REP` helps only a little here, that is expected
- this scenario is less representative for performance claims about irregular memory behavior

### `random_arena`

Each lane gets a fixed-depth chain, but chain nodes are allocated from a shuffled arena.

What it tests:

- irregular pointer chasing
- poor spatial locality
- the effect of a much larger working set

Interpretation:

- this is a core stress case for repeated indirect loads
- useful for comparing cache misses, memory reads, and CPI across `BASE`, `MAA_INDIR_LD_REP`, and `MAA_LOOP`

### `variable_depth_random`

Like `random_arena`, but each lane gets a random depth between `1` and `depth`.

What it tests:

- irregular addresses and irregular repetition counts at the same time
- load imbalance across lanes
- how well both MAA paths handle mixed completion times

Interpretation:

- this is a stronger irregularity test than `random_arena`
- especially useful now that `MAA_LOOP` uses per-hop masking to preserve correctness for variable per-lane `reps`

### `bfs_adj_list`

Builds a BFS-like adjacency-list layout: each lane starts from a shuffled vertex head and walks through a chain of edge nodes.

What it tests:

- graph-style indirection
- adjacency-list traversal behavior
- a realistic irregular workload rather than a synthetic shuffled chain

Interpretation:

- this is a strong default scenario for evaluating `maa_indirect_load_rep`
- if your goal is graph traversal or BFS-like pointer following, this is usually the first scenario to study

### `spmv_gather`

Treats `backing` as a dense vector and gives each lane one random column index into it. Depth is effectively forced to `1`.

What it tests:

- one-hop irregular gather
- sparse-matrix-style vector access
- the cost of a single indirect access rather than repeated pointer chasing

Interpretation:

- use this when you want to isolate the gather-like case
- `depth` is ignored here, so this scenario is not useful for a depth sweep

### `bimodal_depth`

Half the lanes are shallow (`reps=1`), and half are deep (`reps=depth`), with lane assignments shuffled.

What it tests:

- mixed shallow and deep chains in the same instruction
- whether the fused repeated-load path handles strongly unbalanced per-lane work well
- how the fused path compares against repeated single-hop MAA loads under heterogeneous work

Interpretation:

- this is useful when you want a deliberately heterogeneous workload
- it is most meaningful when `depth >= 2`

## Choosing A Scenario

If you are not sure where to start:

- use `bfs_adj_list` for a graph-oriented default
- use `random_arena` for a clean irregular-pointer-chasing stress test
- use `variable_depth_random` if you care about per-lane depth imbalance
- use `spmv_gather` for single-hop gather behavior
- use `uniform_local` as a locality-friendly baseline
- use `bimodal_depth` when you specifically want mixed shallow/deep lanes

## Modes

- `BASE`: runs only the scalar CPU implementation
- `MAA_INDIR_LD_REP`: runs only the fused repeated indirect-load MAA path using `maa_indirect_load_rep`
- `MAA_LOOP`: runs only the looped single-hop MAA path using repeated masked `maa_indirect_load`
- `CMP`: runs `BASE`, `MAA_INDIR_LD_REP`, and `MAA_LOOP`, then checks that both MAA paths match the CPU baseline

Recommended workflow:

1. use `CMP` in `FUNC` mode for quick correctness checks
2. use `BASE`, `MAA_INDIR_LD_REP`, and `MAA_LOOP` in `GEM5` mode for performance studies
3. compare `MAA_INDIR_LD_REP` vs `MAA_LOOP` when you want to isolate the instruction-level benefit of `INDIR_LD_REP`
4. compare `MAA_INDIR_LD_REP` vs `BASE` when you want the system-level CPU-versus-MAA result

## `FUNC` vs `GEM5`

### `FUNC`

`FUNC` uses the functional simulator backend.

Use it for:

- quick correctness testing
- validating new scenarios or parameter choices
- debugging output mismatches

Do not use it for:

- CPI, IPC, cache behavior, or DRAM performance analysis

### `GEM5`

`GEM5` uses the gem5-backed backend.

Use it for:

- performance evaluation
- cache and memory analysis
- measuring the impact of the new instruction

`sweep.sh` is intended for `GEM5` runs.

## Building

From the `API` directory:

```bash
cd /u7/c47fan/cs450/DiX100/benchmarks/API
```

Build the functional version:

```bash
bash make_benchmark_indir_ld_rep.sh FUNC
```

Build the gem5 version:

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
```

You usually want:

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
```

before running the sweep.

## Running A Single Benchmark

### Functional correctness check

```bash
bash make_benchmark_indir_ld_rep.sh FUNC
./benchmark_indir_ld_rep.o 64 8 CMP bfs_adj_list 64
```

### GEM5 single run: fused instruction

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
bash run_benchmark_indir_ld_rep.sh 64 8 MAA_INDIR_LD_REP bfs_adj_list 64
```

### GEM5 single run: looped MAA baseline

```bash
bash run_benchmark_indir_ld_rep.sh 64 8 MAA_LOOP bfs_adj_list 64
```

### GEM5 single run with shared output in `/tmp`

```bash
OUTDIR_MODE=shared OUTDIR_ROOT=/tmp bash run_benchmark_indir_ld_rep.sh 64 8 MAA_INDIR_LD_REP bfs_adj_list 64
```

## How `run_benchmark_indir_ld_rep.sh` Works

The runner:

- checks that `benchmark_indir_ld_rep.o` exists
- finds a usable `gem5.opt` or `gem5.fast`
- launches gem5 with the configured CPU, cache, and memory system
- passes benchmark arguments through `--options`
- writes gem5 outputs to an outdir

Useful environment variables:

- `OUTDIR=/tmp/my_run`
- `OUTDIR_MODE=shared`
- `OUTDIR_ROOT=/tmp`
- `SHARED_OUTDIR_NAME=chkpt_indir_ld_rep_shared`

Why this matters:

- gem5 normally creates an output directory per run
- shared mode is useful for keeping the repo clean during sweeps

## How `sweep.sh` Works

`sweep.sh` automates a parameter sweep over:

- `MODES`
- `N_LIST`
- `DEPTH_LIST`

and keeps `SCENARIO` and `ARENA_MULT` fixed for that sweep.

By default it:

- runs in shared outdir mode
- uses `/tmp` for gem5 output
- sweeps `MAA_INDIR_LD_REP` only unless you set `MODES`
- appends one CSV row per completed run

Default knobs:

- `MODES=MAA_INDIR_LD_REP`
- `SCENARIO=bfs_adj_list`
- `ARENA_MULT=64`
- `N_LIST="64 256 1024"`
- `DEPTH_LIST="2 4 8"`

Important:

- `MAX_RUNS` defaults to the full candidate count
- if you set `MODES="BASE MAA_INDIR_LD_REP MAA_LOOP"`, the sweep will include all three standalone paths
- the CSV contains a `mode` column, so `BASE`, `MAA_INDIR_LD_REP`, and `MAA_LOOP` results stay separate

## Running The Sweep

Typical workflow:

```bash
cd /u7/c47fan/cs450/DiX100/benchmarks/API
bash make_benchmark_indir_ld_rep.sh GEM5
DRY_RUN=1 MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
```

### Run only one scenario

```bash
SCENARIO=bfs_adj_list MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
```

### Run a smaller sweep

```bash
SCENARIO=bfs_adj_list MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" N_LIST="64 256" DEPTH_LIST="4 8" bash sweep.sh
```

### Run a random-pointer stress test

```bash
SCENARIO=random_arena MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
```

### Run a mixed-depth stress test

```bash
SCENARIO=bimodal_depth MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" DEPTH_LIST="2 4 8" bash sweep.sh
```

### Run only the fused-vs-looped comparison

```bash
MODES="MAA_INDIR_LD_REP MAA_LOOP" SCENARIO=bfs_adj_list bash sweep.sh
```

## Sweep Output

The sweep produces two kinds of output:

### Terminal output

- configuration summary
- one line per run
- final started/skipped summary

### CSV output

By default:

```bash
./indir_ld_rep_sweep_results.csv
```

Each row corresponds to one `(mode, scenario, n, depth, arena_mult)` run and includes:

- identifying fields: `mode`, `scenario`, `n`, `depth`, `arena_mult`
- CPU metrics: `cpu0_cpi`, `cpu0_ipc`, `cpu0_cycles`
- cache metrics: read accesses, hits, misses, miss rate, miss latency
- memory metrics: total DRAM reads, bytes read, read bandwidth
- MAA metrics: indirect instruction counts, cycles, cache packets, row-table activity, stage-cycle breakdowns, and DTLB counters

This is the main artifact you should analyze after the sweep.

## Which `stats.txt` Region The Sweep Uses

This detail matters because gem5 `stats.txt` can contain multiple statistics sections in a single file.

For this benchmark:

- `BASE` runs generate two sections
- `MAA_INDIR_LD_REP` runs generate two sections
- `MAA_LOOP` runs generate two sections
- `CMP` runs generate four sections

Why:

- the benchmark calls `m5_reset_stats()` immediately before each measured kernel
- it calls `m5_dump_stats()` immediately after each measured kernel
- gem5 also writes a final end-of-simulation stats section when the program exits

For standalone modes:

- section 1 is the measured kernel region
- section 2 is the final cumulative section after the benchmarked region

For `CMP` mode:

- section 1 is the measured `BASE` kernel region
- section 2 is the measured `MAA_INDIR_LD_REP` kernel region
- section 3 is the measured `MAA_LOOP` kernel region
- section 4 is the final cumulative section after the measured regions

`sweep.sh` is designed for performance sweeps over standalone modes, not `CMP`.
For each run, it extracts the first matching occurrence of each stat key from `stats.txt`.

That means:

- for `BASE`, the sweep uses section 1
- for `MAA_INDIR_LD_REP`, the sweep uses section 1
- for `MAA_LOOP`, the sweep uses section 1
- `CMP` is intentionally not supported by `sweep.sh`

This is the intended behavior, because section 1 is the clean kernel-only measurement for standalone runs. Later sections include trailing benchmark work such as checksum, printing, and cleanup.

## Interpreting The CSV

A good first comparison is to group rows by:

- `scenario`
- `n`
- `depth`
- `arena_mult`

Then compare:

- `BASE` vs `MAA_INDIR_LD_REP`
- `MAA_INDIR_LD_REP` vs `MAA_LOOP`
- `cpu0_cpi`
- `cpu0_ipc`
- `mem_num_reads_total`
- `dcache_read_misses`
- `maa_cycles_indrd`
- `maa_cache_rd_packets`

Questions the CSV can help answer:

- does MAA reduce CPU-side CPI as depth increases?
- does the fused repeated-load instruction beat the looped single-hop MAA path?
- does MAA reduce or increase memory traffic?
- do cache misses scale differently between the CPU baseline and the MAA paths?
- which scenario benefits most from the fused repeated indirect-load instruction?
- is the instruction more helpful for large `n`, deep chains, irregular depths, or all of the above?

## Recommended Experiments

### First correctness pass

```bash
bash make_benchmark_indir_ld_rep.sh FUNC
./benchmark_indir_ld_rep.o 64 8 CMP bfs_adj_list 64
```

### First performance pass

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
SCENARIO=bfs_adj_list MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" N_LIST="64 256" DEPTH_LIST="4 8" bash sweep.sh
```

### Fused-vs-looped instruction study

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
SCENARIO=bfs_adj_list MODES="MAA_INDIR_LD_REP MAA_LOOP" N_LIST="64 256 1024" DEPTH_LIST="1 4 8" bash sweep.sh
```

### Irregularity study

```bash
SCENARIO=random_arena MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
SCENARIO=variable_depth_random MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" bash sweep.sh
SCENARIO=bimodal_depth MODES="BASE MAA_INDIR_LD_REP MAA_LOOP" DEPTH_LIST="2 4 8" bash sweep.sh
```

## Notes And Caveats

- `n` must be positive and no larger than `TILE_SIZE`
- in this build setup, `TILE_SIZE=16384`
- `depth` must be positive
- `spmv_gather` ignores `depth`
- `bimodal_depth` is most meaningful for `depth >= 2`
- many `maa_*` CSV columns will be `NA` for `BASE` rows, which is expected
- the benchmark uses a fixed random seed, so runs are deterministic for a given parameter set
