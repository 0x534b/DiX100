#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit 1

USER_SET_DEPTH_LIST=0
if [ "${DEPTH_LIST+x}" = "x" ]; then
    USER_SET_DEPTH_LIST=1
fi

# Conservative defaults
# Override any of these with environment variables, for example:
#   MODES="BASE MAA" N_LIST="64 256" DEPTH_LIST="1 4 8" MAX_RUNS=4 bash sweep.sh
#   DRY_RUN=1 bash sweep.sh

RUNNER="${RUNNER:-bash run_benchmark_indir_ld_rep.sh}"
OUTDIR_MODE="${OUTDIR_MODE:-shared}"
OUTDIR_ROOT="${OUTDIR_ROOT:-/tmp}"
SHARED_OUTDIR_NAME="${SHARED_OUTDIR_NAME:-chkpt_indir_ld_rep_shared}"
MODES="${MODES:-MAA}"
SCENARIO="${SCENARIO:-bfs_adj_list}"
ARENA_MULT="${ARENA_MULT:-64}"
N_LIST="${N_LIST:-64 256 1024}"
DEPTH_LIST="${DEPTH_LIST:-1 4 8}"
MAX_RUNS="${MAX_RUNS:-}"
SKIP_EXISTING="${SKIP_EXISTING:-1}"
DRY_RUN="${DRY_RUN:-0}"
RESULTS_CSV="${RESULTS_CSV:-./indir_ld_rep_sweep_results.csv}"

usage() {
    cat <<'EOF'
Usage:
  bash sweep.sh

Environment overrides:
  OUTDIR_MODE=shared
  OUTDIR_ROOT=/tmp
  SHARED_OUTDIR_NAME=chkpt_indir_ld_rep_shared
  MODES="MAA" or "BASE MAA"
  SCENARIO="bfs_adj_list"
  ARENA_MULT=64
  N_LIST="64 256 1024"
  DEPTH_LIST="1 4 8"
  MAX_RUNS=<full sweep by default>
  SKIP_EXISTING=1
  DRY_RUN=0
  RESULTS_CSV=./indir_ld_rep_sweep_results.csv

Examples:
  bash sweep.sh
  OUTDIR_MODE=shared OUTDIR_ROOT=/tmp bash sweep.sh
  MODES="BASE MAA" MAX_RUNS=6 bash sweep.sh
  N_LIST="64 128 256 512" DEPTH_LIST="2 4" bash sweep.sh
  DRY_RUN=1 bash sweep.sh

Notes:
  - By default, runs reuse one shared GEM5 outdir in /tmp.
  - If you switch to OUTDIR_MODE=unique, each GEM5 run creates one directory named:
      chkpt_indir_ld_rep_<MODE>_<SCENARIO>_N<N>_D<DEPTH>_A<ARENA_MULT>
  - After each run, the script appends a performance summary row to RESULTS_CSV.
  - Total possible runs = (#MODES * #N values * #DEPTH values)
  - By default, MAX_RUNS is set to the full candidate count.
  - If you set MAX_RUNS explicitly, the sweep stops after that many runs.
EOF
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

count_words() {
    local text="$1"
    local count=0
    for _ in $text; do
        count=$((count + 1))
    done
    echo "$count"
}

for mode in $MODES; do
    if [ "$mode" = "CMP" ]; then
        echo "sweep.sh does not support CMP mode."
        echo "Use MODES=\"BASE MAA\" for performance sweeps."
        echo "Use CMP only for standalone correctness checks."
        exit 1
    fi
done

if [ "$USER_SET_DEPTH_LIST" -eq 0 ]; then
    if [ "$SCENARIO" = "spmv_gather" ]; then
        DEPTH_LIST="1"
    elif [ "$SCENARIO" = "bimodal_depth" ]; then
        DEPTH_LIST="2 4 8"
    fi
fi

if [ "$SCENARIO" = "spmv_gather" ]; then
    if [ "$DEPTH_LIST" != "1" ]; then
        echo "spmv_gather ignores depth; forcing DEPTH_LIST to 1."
        DEPTH_LIST="1"
    fi
elif [ "$SCENARIO" = "bimodal_depth" ]; then
    filtered_depth_list=""
    for depth in $DEPTH_LIST; do
        if [ "$depth" -ge 2 ]; then
            filtered_depth_list="${filtered_depth_list}${filtered_depth_list:+ }${depth}"
        fi
    done
    if [ -z "$filtered_depth_list" ]; then
        echo "bimodal_depth requires depth >= 2."
        echo "Set DEPTH_LIST to values like \"2 4 8\"."
        exit 1
    fi
    if [ "$filtered_depth_list" != "$DEPTH_LIST" ]; then
        echo "bimodal_depth requires depth >= 2; dropping invalid depths from DEPTH_LIST."
        DEPTH_LIST="$filtered_depth_list"
    fi
elif [ "$SCENARIO" = "uniform_local" ]; then
    echo "Note: uniform_local ignores arena_mult; it is kept only for a consistent CLI."
fi

mode_count="$(count_words "$MODES")"
n_count="$(count_words "$N_LIST")"
depth_count="$(count_words "$DEPTH_LIST")"
total_candidates=$((mode_count * n_count * depth_count))

if [ -z "$MAX_RUNS" ]; then
    MAX_RUNS="$total_candidates"
fi

echo "Sweep configuration:"
echo "  runner        : $RUNNER"
echo "  outdir mode   : $OUTDIR_MODE"
echo "  outdir root   : $OUTDIR_ROOT"
echo "  shared name   : $SHARED_OUTDIR_NAME"
echo "  modes         : $MODES"
echo "  scenario      : $SCENARIO"
echo "  arena_mult    : $ARENA_MULT"
echo "  n values      : $N_LIST"
echo "  depth values  : $DEPTH_LIST"
echo "  candidate runs: $total_candidates"
echo "  max runs      : $MAX_RUNS"
echo "  skip existing : $SKIP_EXISTING"
echo "  dry run       : $DRY_RUN"
echo "  results csv   : $RESULTS_CSV"
echo

if [ "$MAX_RUNS" -le 0 ]; then
    echo "MAX_RUNS must be > 0"
    exit 1
fi

if [ ! -f "$RESULTS_CSV" ]; then
    echo "mode,scenario,arena_mult,n,depth,outdir,sim_ticks,sim_insts,host_seconds,host_tick_rate,cpu0_cycles,cpu0_cpi,cpu0_ipc,dcache_read_accesses,dcache_read_hits,dcache_read_misses,dcache_read_miss_rate,dcache_read_avg_miss_latency,dcache_demand_hits,dcache_demand_misses,l2_overall_hits,l2_overall_misses,l3_overall_hits,l3_overall_misses,mem_num_reads_total,mem_bytes_read_total,mem_bw_read_total,maa_num_inst_total,maa_num_inst_indrd,maa_cycles_total,maa_cycles_busy,maa_cycles_idle,maa_cycles_indrd,maa_avg_cpi_indrd,maa_cache_rd_packets,maa_cache_bw,maa_words_inserted,maa_cachelines_inserted,maa_rows_inserted,maa_unique_words,maa_unique_cachelines,maa_unique_rows,maa_cycles_fill,maa_cycles_request,maa_cycles_rt_access,maa_cycles_spd_read,maa_cycles_spd_write,maa_load_hit_count,maa_avg_load_hit_latency,maa_dtb_read_accesses,maa_dtb_read_misses" > "$RESULTS_CSV"
fi

extract_stat() {
    local stats_file="$1"
    local key="$2"
    awk -v key="$key" '$1 == key { print $2; exit }' "$stats_file"
}

run_count=0
skip_count=0

for mode in $MODES; do
    for n in $N_LIST; do
        for depth in $DEPTH_LIST; do
            if [ "$run_count" -ge "$MAX_RUNS" ]; then
                echo "Reached MAX_RUNS=$MAX_RUNS, stopping early."
                echo "Started runs: $run_count"
                echo "Skipped existing: $skip_count"
                exit 0
            fi

            if [ "$OUTDIR_MODE" = "shared" ]; then
                outdir="${OUTDIR_ROOT%/}/${SHARED_OUTDIR_NAME}"
            else
                outdir="${OUTDIR_ROOT%/}/chkpt_indir_ld_rep_${mode}_${SCENARIO}_N${n}_D${depth}_A${ARENA_MULT}"
            fi
            cmd="OUTDIR_MODE=$OUTDIR_MODE OUTDIR_ROOT=$OUTDIR_ROOT SHARED_OUTDIR_NAME=$SHARED_OUTDIR_NAME $RUNNER $n $depth $mode $SCENARIO $ARENA_MULT"

            if [ "$OUTDIR_MODE" = "unique" ] && [ "$SKIP_EXISTING" = "1" ] && [ -d "$outdir" ]; then
                echo "Skipping existing: $outdir"
                skip_count=$((skip_count + 1))
                continue
            fi

            echo "[$((run_count + 1))/$MAX_RUNS] $cmd"
            if [ "$DRY_RUN" != "1" ]; then
                eval "$cmd"

                stats_file="${outdir}/stats.txt"
                if [ -f "$stats_file" ]; then
                    sim_ticks="$(extract_stat "$stats_file" "simTicks")"
                    sim_insts="$(extract_stat "$stats_file" "simInsts")"
                    host_seconds="$(extract_stat "$stats_file" "hostSeconds")"
                    host_tick_rate="$(extract_stat "$stats_file" "hostTickRate")"
                    cpu0_cycles="$(extract_stat "$stats_file" "system.cpu0.numCycles")"
                    cpu0_cpi="$(extract_stat "$stats_file" "system.cpu0.cpi")"
                    cpu0_ipc="$(extract_stat "$stats_file" "system.cpu0.ipc")"

                    dcache_read_accesses="$(extract_stat "$stats_file" "system.cpu0.dcache.ReadReq_T.accesses::total")"
                    dcache_read_hits="$(extract_stat "$stats_file" "system.cpu0.dcache.ReadReq_T.hits::total")"
                    dcache_read_misses="$(extract_stat "$stats_file" "system.cpu0.dcache.ReadReq_T.misses::total")"
                    dcache_read_miss_rate="$(extract_stat "$stats_file" "system.cpu0.dcache.ReadReq_T.missRate::total")"
                    dcache_read_avg_miss_latency="$(extract_stat "$stats_file" "system.cpu0.dcache.ReadReq_T.avgMissLatency::total")"
                    dcache_demand_hits="$(extract_stat "$stats_file" "system.cpu0.dcache.demandHits_T::total")"
                    dcache_demand_misses="$(extract_stat "$stats_file" "system.cpu0.dcache.demandMisses_T::total")"
                    l2_overall_hits="$(extract_stat "$stats_file" "system.cpu0.l2cache.overallHits::total")"
                    l2_overall_misses="$(extract_stat "$stats_file" "system.cpu0.l2cache.overallMisses::total")"
                    l3_overall_hits="$(extract_stat "$stats_file" "system.l3.overallHits::total")"
                    l3_overall_misses="$(extract_stat "$stats_file" "system.l3.overallMisses::total")"
                    mem_num_reads_total="$(extract_stat "$stats_file" "system.mem_ctrls.numReads::total")"
                    mem_bytes_read_total="$(extract_stat "$stats_file" "system.mem_ctrls.bytesRead::total")"
                    mem_bw_read_total="$(extract_stat "$stats_file" "system.mem_ctrls.bwRead::total")"

                    maa_num_inst_total="$(extract_stat "$stats_file" "system.maa.numInst")"
                    maa_num_inst_indrd="$(extract_stat "$stats_file" "system.maa.numInst_INDRD")"
                    maa_cycles_total="$(extract_stat "$stats_file" "system.maa.cycles_TOTAL")"
                    maa_cycles_busy="$(extract_stat "$stats_file" "system.maa.cycles_BUSY")"
                    maa_cycles_idle="$(extract_stat "$stats_file" "system.maa.cycles_IDLE")"
                    maa_cycles_indrd="$(extract_stat "$stats_file" "system.maa.cycles_INDRD")"
                    maa_avg_cpi_indrd="$(extract_stat "$stats_file" "system.maa.avgCPI_INDRD")"
                    maa_cache_rd_packets="$(extract_stat "$stats_file" "system.maa.port_cache_RD_packets")"
                    maa_cache_bw="$(extract_stat "$stats_file" "system.maa.port_cache_BW")"
                    maa_words_inserted="$(extract_stat "$stats_file" "system.maa.I0_IND_NumWordsInserted")"
                    maa_cachelines_inserted="$(extract_stat "$stats_file" "system.maa.I0_IND_NumCacheLineInserted")"
                    maa_rows_inserted="$(extract_stat "$stats_file" "system.maa.I0_IND_NumRowsInserted")"
                    maa_unique_words="$(extract_stat "$stats_file" "system.maa.I0_IND_NumUniqueWordsInserted")"
                    maa_unique_cachelines="$(extract_stat "$stats_file" "system.maa.I0_IND_NumUniqueCacheLineInserted")"
                    maa_unique_rows="$(extract_stat "$stats_file" "system.maa.I0_IND_NumUniqueRowsInserted")"
                    maa_cycles_fill="$(extract_stat "$stats_file" "system.maa.I0_IND_CyclesFill")"
                    maa_cycles_request="$(extract_stat "$stats_file" "system.maa.I0_IND_CyclesRequest")"
                    maa_cycles_rt_access="$(extract_stat "$stats_file" "system.maa.I0_IND_CyclesRTAccess")"
                    maa_cycles_spd_read="$(extract_stat "$stats_file" "system.maa.I0_IND_CyclesSPDReadAccess")"
                    maa_cycles_spd_write="$(extract_stat "$stats_file" "system.maa.I0_IND_CyclesSPDWriteAccess")"
                    maa_load_hit_count="$(extract_stat "$stats_file" "system.maa.I0_IND_LoadsCacheHitAccessing")"
                    maa_avg_load_hit_latency="$(extract_stat "$stats_file" "system.maa.I0_IND_AvgLoadsCacheHitAccessingLatency")"
                    maa_dtb_read_accesses="$(extract_stat "$stats_file" "system.maa.mmu.dtb.rdAccesses")"
                    maa_dtb_read_misses="$(extract_stat "$stats_file" "system.maa.mmu.dtb.rdMisses")"

                    echo "${mode},${SCENARIO},${ARENA_MULT},${n},${depth},${outdir},${sim_ticks:-NA},${sim_insts:-NA},${host_seconds:-NA},${host_tick_rate:-NA},${cpu0_cycles:-NA},${cpu0_cpi:-NA},${cpu0_ipc:-NA},${dcache_read_accesses:-NA},${dcache_read_hits:-NA},${dcache_read_misses:-NA},${dcache_read_miss_rate:-NA},${dcache_read_avg_miss_latency:-NA},${dcache_demand_hits:-NA},${dcache_demand_misses:-NA},${l2_overall_hits:-NA},${l2_overall_misses:-NA},${l3_overall_hits:-NA},${l3_overall_misses:-NA},${mem_num_reads_total:-NA},${mem_bytes_read_total:-NA},${mem_bw_read_total:-NA},${maa_num_inst_total:-NA},${maa_num_inst_indrd:-NA},${maa_cycles_total:-NA},${maa_cycles_busy:-NA},${maa_cycles_idle:-NA},${maa_cycles_indrd:-NA},${maa_avg_cpi_indrd:-NA},${maa_cache_rd_packets:-NA},${maa_cache_bw:-NA},${maa_words_inserted:-NA},${maa_cachelines_inserted:-NA},${maa_rows_inserted:-NA},${maa_unique_words:-NA},${maa_unique_cachelines:-NA},${maa_unique_rows:-NA},${maa_cycles_fill:-NA},${maa_cycles_request:-NA},${maa_cycles_rt_access:-NA},${maa_cycles_spd_read:-NA},${maa_cycles_spd_write:-NA},${maa_load_hit_count:-NA},${maa_avg_load_hit_latency:-NA},${maa_dtb_read_accesses:-NA},${maa_dtb_read_misses:-NA}" >> "$RESULTS_CSV"
                    echo "  appended results to $RESULTS_CSV"
                else
                    echo "  warning: stats file not found at ${stats_file}"
                fi
            fi
            run_count=$((run_count + 1))
            echo
        done
    done
done

echo "Sweep finished."
echo "Started runs: $run_count"
echo "Skipped existing: $skip_count"
