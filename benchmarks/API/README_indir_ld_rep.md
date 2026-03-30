# `benchmark_indir_ld_rep`

This benchmark evaluates the repeated indirect-load path implemented by:

- the CPU baseline loop (`BASE`)
- the MAA instruction path (`MAA`)
- correctness comparison between them (`CMP`)

At a high level, each lane starts from an index in `starts[i]`, follows a pointer chain in `backing`, and repeats that dereference `reps[i]` times. The MAA path exercises `maa_indirect_load_rep`, while the baseline path performs the same work as a scalar CPU loop.

## What The Benchmark Is Measuring

The benchmark is meant to answer:

- how performance changes as the number of active lanes `n` increases
- how performance changes as indirect-chain depth increases
- how the new repeated indirect-load instruction compares against the baseline CPU implementation
- whether the instruction behaves differently under regular, irregular, shallow, deep, and mixed-depth access patterns

The main knobs are:

- `n`: number of active lanes
- `depth`: maximum pointer-chain depth
- `scenario`: the access pattern used to populate `starts`, `reps`, and `backing`
- `arena_mult`: working-set expansion factor for scenarios that randomize addresses

## Command-Line Interface

The benchmark binary accepts:

```bash
./benchmark_indir_ld_rep.o <n> <depth> <BASE|MAA|CMP> <scenario> [arena_mult]
```

Example:

```bash
./benchmark_indir_ld_rep.o 64 8 CMP bfs_adj_list 64
```

Meaning:

- `64` active lanes
- maximum depth `8`
- run both baseline and MAA and compare outputs
- use the `bfs_adj_list` scenario
- use `arena_mult=64`

## Scenarios

The benchmark currently supports six scenarios.

### `uniform_local`

Each lane gets a compact, contiguous pointer chain laid out locally in memory.

What it tests:

- best-case spatial locality
- minimal address randomness
- whether the new instruction still helps when the baseline already has a friendly memory layout

Interpretation:

- this is the easiest case for caches and prefetching
- if MAA only helps here a little, that is expected
- if MAA helps a lot here too, that suggests lower control overhead or better overlap even in a friendly case

### `random_arena`

Each lane gets a fixed-depth chain, but chain nodes are allocated from a shuffled arena.

What it tests:

- irregular pointer chasing
- poor spatial locality
- the effect of a much larger working set

Interpretation:

- this is a core stress case for repeated indirect loads
- useful for comparing cache misses, memory reads, and CPI between `BASE` and `MAA`

### `variable_depth_random`

Like `random_arena`, but each lane gets a random depth between `1` and `depth`.

What it tests:

- irregular addresses and irregular repetition counts at the same time
- load imbalance across lanes
- how well the new instruction handles mixed completion times

Interpretation:

- this is a stronger irregularity test than `random_arena`
- good for checking whether MAA still behaves well when `reps[i]` varies per lane

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
- whether the new instruction handles strongly unbalanced per-lane work well
- robustness against a simple sequential grouping that a prefetcher might exploit

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

## `BASE`, `MAA`, And `CMP`

- `BASE`: runs only the scalar CPU implementation
- `MAA`: runs only the MAA implementation using `maa_indirect_load_rep`
- `CMP`: runs both and checks that their outputs match

Recommended workflow:

1. use `CMP` in `FUNC` mode for quick correctness checks
2. use `BASE` and `MAA` in `GEM5` mode for actual performance studies

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

### GEM5 single run

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
bash run_benchmark_indir_ld_rep.sh 64 8 MAA bfs_adj_list 64
```

### GEM5 single run with shared output in `/tmp`

```bash
OUTDIR_MODE=shared OUTDIR_ROOT=/tmp bash run_benchmark_indir_ld_rep.sh 64 8 MAA bfs_adj_list 64
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
- sweeps `MAA` only unless you set `MODES`
- appends one CSV row per completed run

Default knobs:

- `MODES=MAA`
- `SCENARIO=bfs_adj_list`
- `ARENA_MULT=64`
- `N_LIST="64 256 1024"`
- `DEPTH_LIST="1 4 8"`

Important:

- `MAX_RUNS` defaults to the full candidate count
- if you set `MODES="BASE MAA"`, the sweep will include both
- the CSV contains a `mode` column, so `BASE` and `MAA` results stay separate

## Running The Sweep

Typical workflow:

```bash
cd /u7/c47fan/cs450/DiX100/benchmarks/API
bash make_benchmark_indir_ld_rep.sh GEM5
DRY_RUN=1 MODES="BASE MAA" bash sweep.sh
MODES="BASE MAA" bash sweep.sh
```

### Run only one scenario

```bash
SCENARIO=bfs_adj_list MODES="BASE MAA" bash sweep.sh
```

### Run a smaller sweep

```bash
SCENARIO=bfs_adj_list MODES="BASE MAA" N_LIST="64 256" DEPTH_LIST="4 8" bash sweep.sh
```

### Run a random-pointer stress test

```bash
SCENARIO=random_arena MODES="BASE MAA" bash sweep.sh
```

### Run a mixed-depth stress test

```bash
SCENARIO=bimodal_depth MODES="BASE MAA" DEPTH_LIST="2 4 8" bash sweep.sh
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
- `MAA` runs generate two sections
- `CMP` runs generate three sections

Why:

- the benchmark calls `m5_reset_stats()` immediately before the measured kernel
- it calls `m5_dump_stats()` immediately after that kernel
- gem5 also writes a final end-of-simulation stats section when the program exits

For `BASE` mode:

- section 1 is the measured baseline kernel region
- section 2 is the final cumulative section after the benchmarked region

For `MAA` mode:

- section 1 is the measured MAA kernel region
- section 2 is the final cumulative section after the benchmarked region

For `CMP` mode:

- section 1 is the measured baseline kernel region
- section 2 is the measured MAA kernel region
- section 3 is the final cumulative section after the second measured region

`sweep.sh` is designed for performance sweeps over `BASE` and `MAA`, not `CMP`.
For each run, it extracts the first matching occurrence of each stat key from `stats.txt`.

That means:

- for `BASE`, the sweep uses section 1
- for `MAA`, the sweep uses section 1
- `CMP` is intentionally not supported by `sweep.sh`

This is the intended behavior, because section 1 is the clean kernel-only measurement for standalone `BASE` and standalone `MAA` runs.
The later stats sections include trailing benchmark work such as checksum/output/cleanup and are not the primary per-kernel measurement you usually want in the CSV.

## Interpreting The CSV

A good first comparison is to group rows by:

- `scenario`
- `n`
- `depth`
- `arena_mult`

Then compare:

- `BASE` vs `MAA`
- `cpu0_cpi`
- `cpu0_ipc`
- `mem_num_reads_total`
- `dcache_read_misses`
- `maa_cycles_indrd`
- `maa_cache_rd_packets`

Questions the CSV can help answer:

- does MAA reduce CPU-side CPI as depth increases?
- does MAA reduce or increase memory traffic?
- do cache misses scale differently between baseline and MAA?
- which scenario benefits most from the new instruction?
- is the instruction more helpful for large `n`, deep chains, or both?

## Recommended Experiments

### First correctness pass

```bash
bash make_benchmark_indir_ld_rep.sh FUNC
./benchmark_indir_ld_rep.o 64 8 CMP bfs_adj_list 64
```

### First performance pass

```bash
bash make_benchmark_indir_ld_rep.sh GEM5
SCENARIO=bfs_adj_list MODES="BASE MAA" N_LIST="64 256" DEPTH_LIST="4 8" bash sweep.sh
```

### Irregularity study

```bash
SCENARIO=random_arena MODES="BASE MAA" bash sweep.sh
SCENARIO=variable_depth_random MODES="BASE MAA" bash sweep.sh
SCENARIO=bimodal_depth MODES="BASE MAA" DEPTH_LIST="2 4 8" bash sweep.sh
```

## Notes And Caveats

- `n` must be positive and no larger than `TILE_SIZE`
- in this build setup, `TILE_SIZE=16384`
- `depth` must be positive
- `spmv_gather` ignores `depth`
- `bimodal_depth` is most meaningful for `depth >= 2`
- many `maa_*` CSV columns will be `NA` for `BASE` rows, which is expected
- the benchmark uses a fixed random seed, so runs are deterministic for a given parameter set
