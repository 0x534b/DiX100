#include "MAA.hpp"
#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#if !defined(FUNC) && !defined(GEM5) && !defined(GEM5_MAGIC)
#define FUNC
#endif

#if defined(FUNC)
#include "MAA_functional.hpp"
#elif defined(GEM5)
#include "MAA_gem5.hpp"
#include <gem5/m5ops.h>
#elif defined(GEM5_MAGIC)
#include "MAA_gem5_magic.hpp"
#endif

static volatile uint64_t global_sink = 0;

enum class ScenarioKind {
    UniformLocal,
    RandomArena,
    VariableDepthRandom,
    BfsAdjList,
    SpMVGather,
    BimodalDepth
};

const uint32_t kUnusedEntry = 0xffffffffu;
const uint32_t kScenarioSeed = 0x00c47fa1u;

void print_usage(const char *name) {
    std::cout << "Usage: " << name
              << " <n> <depth> [BASE|MAA_INDIR_LD_REP|MAA_LOOP|CMP] [uniform_local|random_arena|variable_depth_random|bfs_adj_list|spmv_gather|bimodal_depth] [arena_mult]"
              << std::endl;
}

const char *scenario_name(ScenarioKind scenario) {
    switch (scenario) {
    case ScenarioKind::UniformLocal:
        return "uniform_local";
    case ScenarioKind::RandomArena:
        return "random_arena";
    case ScenarioKind::VariableDepthRandom:
        return "variable_depth_random";
    case ScenarioKind::BfsAdjList:
        return "bfs_adj_list";
    case ScenarioKind::SpMVGather:
        return "spmv_gather";
    case ScenarioKind::BimodalDepth:
        return "bimodal_depth";
    }
    return "unknown";
}

bool parse_scenario(const std::string &name, ScenarioKind &scenario) {
    if (name == "uniform_local") {
        scenario = ScenarioKind::UniformLocal;
        return true;
    }
    if (name == "random_arena") {
        scenario = ScenarioKind::RandomArena;
        return true;
    }
    if (name == "variable_depth_random") {
        scenario = ScenarioKind::VariableDepthRandom;
        return true;
    }
    if (name == "bfs_adj_list") {
        scenario = ScenarioKind::BfsAdjList;
        return true;
    }
    if (name == "spmv_gather") {
        scenario = ScenarioKind::SpMVGather;
        return true;
    }
    if (name == "bimodal_depth") {
        scenario = ScenarioKind::BimodalDepth;
        return true;
    }
    return false;
}

size_t compact_chain_elements(int n, int depth) {
    return static_cast<size_t>(n) * static_cast<size_t>(depth + 1);
}

size_t scenario_backing_elements(int n, int depth, ScenarioKind scenario, int arena_mult) {
    size_t compact_elems = compact_chain_elements(n, depth);
    if (scenario == ScenarioKind::UniformLocal) {
        return compact_elems;
    }
    if (scenario == ScenarioKind::SpMVGather) {
        // Backing array represents the dense vector being gathered from.
        // Its size is arena_mult * n, independent of depth (which is forced to 1).
        // A larger arena_mult means sparser column indices and a larger working set.
        return static_cast<size_t>(n) * static_cast<size_t>(arena_mult);
    }
    return compact_elems * static_cast<size_t>(arena_mult);
}

uint32_t make_terminal_value(int lane, uint32_t reps) {
    return 0x9e3779b9u ^ (static_cast<uint32_t>(lane) * 2654435761u) ^ (reps * 2246822519u);
}

void clear_arrays(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n) {
    std::fill(backing, backing + backing_elems, kUnusedEntry);
    std::fill(starts, starts + n, 0u);
    std::fill(reps, reps + n, 0u);
}

void init_uniform_local(uint32_t *backing, uint32_t *starts, uint32_t *reps, int n, int depth) {
    for (int lane = 0; lane < n; lane++) {
        int lane_base = lane * (depth + 1);
        for (int hop = 0; hop < depth; hop++) {
            backing[lane_base + hop] = static_cast<uint32_t>(lane_base + hop + 1);
        }
        backing[lane_base + depth] = make_terminal_value(lane, static_cast<uint32_t>(depth));
        starts[lane] = static_cast<uint32_t>(lane_base);
        reps[lane] = static_cast<uint32_t>(depth);
    }
}

void init_randomized_chains(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n,
                            int depth, bool variable_depth) {
    std::vector<uint32_t> shuffled_indices(backing_elems);
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0u);
    std::mt19937 rng(kScenarioSeed);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), rng);
    std::uniform_int_distribution<int> depth_dist(1, depth);

    size_t cursor = 0;
    for (int lane = 0; lane < n; lane++) {
        uint32_t lane_reps = static_cast<uint32_t>(variable_depth ? depth_dist(rng) : depth);
        assert(cursor + lane_reps + 1 <= shuffled_indices.size());
        starts[lane] = shuffled_indices[cursor];
        reps[lane] = lane_reps;
        for (uint32_t hop = 0; hop < lane_reps; hop++) {
            backing[shuffled_indices[cursor + hop]] = shuffled_indices[cursor + hop + 1];
        }
        backing[shuffled_indices[cursor + lane_reps]] = make_terminal_value(lane, lane_reps);
        cursor += lane_reps + 1;
    }
}

void init_bfs_adj_list(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n, int depth) {
    std::vector<uint32_t> shuffled_indices(backing_elems);
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0u);
    std::mt19937 rng(kScenarioSeed ^ 0x85ebca6bu);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), rng);

    size_t cursor = 0;
    std::vector<uint32_t> vertex_heads(n);
    for (int vertex = 0; vertex < n; vertex++) {
        assert(cursor < shuffled_indices.size());
        vertex_heads[vertex] = shuffled_indices[cursor++];
    }

    std::vector<std::vector<uint32_t> > edge_nodes(n, std::vector<uint32_t>(depth));
    for (int vertex = 0; vertex < n; vertex++) {
        for (int hop = 0; hop < depth; hop++) {
            assert(cursor < shuffled_indices.size());
            edge_nodes[vertex][hop] = shuffled_indices[cursor++];
        }
    }

    std::vector<int> frontier_vertices(n);
    std::iota(frontier_vertices.begin(), frontier_vertices.end(), 0);
    std::shuffle(frontier_vertices.begin(), frontier_vertices.end(), rng);

    for (int lane = 0; lane < n; lane++) {
        int vertex = frontier_vertices[lane];
        starts[lane] = vertex_heads[vertex];
        reps[lane] = static_cast<uint32_t>(depth);
        backing[vertex_heads[vertex]] = edge_nodes[vertex][0];
        for (int hop = 0; hop < depth - 1; hop++) {
            backing[edge_nodes[vertex][hop]] = edge_nodes[vertex][hop + 1];
        }
        backing[edge_nodes[vertex][depth - 1]] = make_terminal_value(vertex, static_cast<uint32_t>(depth));
    }
}

void init_spmv_gather(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n) {
    // Fill the dense vector with distinct nonzero values (not kUnusedEntry).
    std::mt19937 rng(kScenarioSeed ^ 0x4b3e92c7u);
    std::uniform_int_distribution<uint32_t> val_dist(1u, 0xfffffffeu);
    for (size_t i = 0; i < backing_elems; i++) {
        backing[i] = val_dist(rng);
    }

    // Assign each lane a random column index into the dense vector.
    // Use sampling without replacement so no two lanes hit the same entry,
    // which reflects a typical unstructured sparse matrix row.
    std::vector<uint32_t> col_indices(backing_elems);
    std::iota(col_indices.begin(), col_indices.end(), 0u);
    std::shuffle(col_indices.begin(), col_indices.end(), rng);

    for (int lane = 0; lane < n; lane++) {
        starts[lane] = col_indices[lane];
        reps[lane] = 1u;
    }
}

void init_bimodal_depth(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n, int depth) {
    std::vector<uint32_t> shuffled_indices(backing_elems);
    std::iota(shuffled_indices.begin(), shuffled_indices.end(), 0u);
    std::mt19937 rng(kScenarioSeed ^ 0x2d5ea4f3u);
    std::shuffle(shuffled_indices.begin(), shuffled_indices.end(), rng);

    // Decide which lanes are shallow (reps=1) and which are deep (reps=depth).
    // Shuffle lane assignments so shallow and deep lanes are interleaved rather
    // than contiguous — this prevents any sequential prefetcher from exploiting
    // the grouping.
    int n_shallow = n / 2;
    std::vector<int> lane_order(n);
    std::iota(lane_order.begin(), lane_order.end(), 0);
    std::shuffle(lane_order.begin(), lane_order.end(), rng);

    std::vector<uint32_t> lane_reps_vec(n);
    for (int i = 0; i < n; i++) {
        lane_reps_vec[lane_order[i]] = (i < n_shallow) ? 1u : static_cast<uint32_t>(depth);
    }

    // Allocate nodes from the shuffled arena. Each lane needs (lane_reps + 1)
    // slots: one per hop plus one terminal.
    size_t cursor = 0;
    for (int lane = 0; lane < n; lane++) {
        uint32_t lane_reps = lane_reps_vec[lane];
        assert(cursor + lane_reps + 1 <= shuffled_indices.size());
        starts[lane] = shuffled_indices[cursor];
        reps[lane] = lane_reps;
        for (uint32_t hop = 0; hop < lane_reps; hop++) {
            backing[shuffled_indices[cursor + hop]] = shuffled_indices[cursor + hop + 1];
        }
        backing[shuffled_indices[cursor + lane_reps]] = make_terminal_value(lane, lane_reps);
        cursor += lane_reps + 1;
    }
}

void init_index_chains(uint32_t *backing, size_t backing_elems, uint32_t *starts, uint32_t *reps, int n, int depth,
                       ScenarioKind scenario, int arena_mult) {
    assert(depth >= 1);
    clear_arrays(backing, backing_elems, starts, reps, n);
    switch (scenario) {
    case ScenarioKind::UniformLocal:
        init_uniform_local(backing, starts, reps, n, depth);
        break;
    case ScenarioKind::RandomArena:
        init_randomized_chains(backing, backing_elems, starts, reps, n, depth, false);
        break;
    case ScenarioKind::VariableDepthRandom:
        init_randomized_chains(backing, backing_elems, starts, reps, n, depth, true);
        break;
    case ScenarioKind::BfsAdjList:
        init_bfs_adj_list(backing, backing_elems, starts, reps, n, depth);
        break;
    case ScenarioKind::SpMVGather:
        // depth is ignored: SpMV gather is always a single indirect load (reps=1).
        init_spmv_gather(backing, backing_elems, starts, reps, n);
        break;
    case ScenarioKind::BimodalDepth:
        // depth should be >= 2
        init_bimodal_depth(backing, backing_elems, starts, reps, n, depth);
        break;
    }
}

uint32_t follow_chain(const uint32_t *backing, uint32_t start, uint32_t reps) {
    uint32_t current = start;
    for (uint32_t hop = 0; hop < reps; hop++) {
        current = backing[current];
    }
    return current;
}

void print_lane_samples(const uint32_t *backing, const uint32_t *starts, const uint32_t *reps, int n) {
    int sample_lanes = std::min(n, 8);
    std::cout << "sampled lanes:" << std::endl;
    for (int lane = 0; lane < sample_lanes; lane++) {
        uint32_t path_limit = std::min<uint32_t>(reps[lane], 6u);
        uint32_t current = starts[lane];
        std::cout << "  lane " << lane << " start=" << starts[lane] << " reps=" << reps[lane] << " path";
        for (uint32_t hop = 0; hop < path_limit; hop++) {
            std::cout << " " << current;
            current = backing[current];
        }
        if (reps[lane] > path_limit) {
            std::cout << " ...";
        }
        std::cout << " -> " << follow_chain(backing, starts[lane], reps[lane]) << std::endl;
    }
}

void indir_ld_rep_baseline(uint32_t *out, uint32_t *backing, const uint32_t *starts, const uint32_t *reps, int n) {
#ifdef GEM5
    m5_work_begin(0, 0);
    m5_reset_stats(0, 0);
#endif
    for (int i = 0; i < n; i++) {
        uint32_t current = starts[i];
        for (uint32_t hop = 0; hop < reps[i]; hop++) {
            current = backing[current];
        }
        out[i] = current;
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(0, 0);
#endif
}

void indir_ld_rep_maa(uint32_t *out, uint32_t *backing, const uint32_t *starts, const uint32_t *reps, int n) {
    init_MAA();

    int max_reg = get_new_reg<int>(n);
    int min_reg = get_new_reg<int>(0);
    int stride_reg = get_new_reg<int>(1);
    int addr_tile = get_new_tile<uint64_t>();
    int rep_tile = get_new_tile<uint32_t>();
    int out_tile = get_new_tile<uint32_t>();
    maa_stream_load<uint32_t>(const_cast<uint32_t *>(starts), min_reg, max_reg, stride_reg, addr_tile);
    maa_stream_load<uint32_t>(const_cast<uint32_t *>(reps), min_reg, max_reg, stride_reg, rep_tile);

#ifdef GEM5
    m5_work_begin(1, 0);
    m5_reset_stats(0, 0);
#endif
    maa_indirect_load_rep<uint32_t>(backing, addr_tile, out_tile, rep_tile);
    wait_ready(out_tile);
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(1, 0);
#endif

    uint32_t *out_tile_ptr = get_cacheable_tile_pointer<uint32_t>(out_tile);
    for (int i = 0; i < n; i++) {
        out[i] = out_tile_ptr[i];
    }
}

void indir_ld_maa_loop(uint32_t *out, uint32_t *backing, const uint32_t *starts, const uint32_t *reps, int n) {
    init_MAA();

    int max_reg = get_new_reg<int>(n);
    int min_reg = get_new_reg<int>(0);
    int stride_reg = get_new_reg<int>(1);
    int zero_reg = get_new_reg<uint32_t>(0u);
    int hop_reg = get_new_reg<uint32_t>(0u);
    int idx_tile = get_new_tile<uint32_t>();
    int rep_tile = get_new_tile<uint32_t>();
    int next_idx_tile = get_new_tile<uint32_t>();
    int active_tile = get_new_tile<uint32_t>();

    maa_stream_load<uint32_t>(const_cast<uint32_t *>(starts), min_reg, max_reg, stride_reg, idx_tile);
    maa_stream_load<uint32_t>(const_cast<uint32_t *>(reps), min_reg, max_reg, stride_reg, rep_tile);
    wait_ready(idx_tile);
    wait_ready(rep_tile);

    uint32_t max_reps = *std::max_element(reps, reps + n);

#ifdef GEM5
    m5_work_begin(2, 0);
    m5_reset_stats(0, 0);
#endif
    for (uint32_t hop = 0; hop < max_reps; hop++) {
        set_reg<uint32_t>(hop_reg, hop);
        maa_alu_scalar<uint32_t>(rep_tile, hop_reg, active_tile, Operation_t::GT_OP);
        wait_ready(active_tile);

        // Preserve lanes that are already done before conditionally advancing active lanes.
        maa_alu_scalar<uint32_t>(idx_tile, zero_reg, next_idx_tile, Operation_t::ADD_OP);
        wait_ready(next_idx_tile);

        maa_indirect_load<uint32_t>(backing, idx_tile, next_idx_tile, active_tile);
        wait_ready(next_idx_tile);

        std::swap(idx_tile, next_idx_tile);
    }
#ifdef GEM5
    m5_dump_stats(0, 0);
    m5_work_end(2, 0);
#endif

    uint32_t *out_tile_ptr = get_cacheable_tile_pointer<uint32_t>(idx_tile);
    for (int i = 0; i < n; i++) {
        out[i] = out_tile_ptr[i];
    }
}

uint32_t checksum(const uint32_t *data, int n) {
    uint64_t sum = 0;
    for (int i = 0; i < n; i++) {
        sum ^= data[i] + 0x7f4a7c15UL + (sum << 6) + (sum >> 2);
    }
    return sum;
}

bool compare_arrays(const uint32_t *expected, const uint32_t *actual, int n) {
    for (int i = 0; i < n; i++) {
        if (expected[i] != actual[i]) {
            std::cout << "Mismatch at " << i << ": expected " << expected[i]
                      << ", got " << actual[i] << std::endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 4 || argc > 6) {
        print_usage(argv[0]);
        return 1;
    }

    int n = std::stoi(argv[1]);
    int depth = std::stoi(argv[2]);
    std::string mode = argv[3];
    ScenarioKind scenario = ScenarioKind::UniformLocal;
    if (argc >= 5 && !parse_scenario(argv[4], scenario)) {
        print_usage(argv[0]);
        return 1;
    }
    int arena_mult = argc >= 6 ? std::stoi(argv[5]) : 64;
    bool run_base = mode == "BASE" || mode == "CMP";
    bool run_maa = mode == "MAA_INDIR_LD_REP" || mode == "CMP";
    bool run_maa_loop = mode == "MAA_LOOP" || mode == "CMP";
    if ((!run_base && !run_maa && !run_maa_loop) || n <= 0 || n > TILE_SIZE || depth <= 0 || arena_mult <= 0) {
        print_usage(argv[0]);
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    size_t backing_elems = scenario_backing_elements(n, depth, scenario, arena_mult);
    uint32_t *backing = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * backing_elems));
    uint32_t *starts = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    uint32_t *reps = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    uint32_t *base_out = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    uint32_t *maa_out = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    uint32_t *maa_loop_out = static_cast<uint32_t *>(malloc(sizeof(uint32_t) * n));
    if (backing == nullptr || starts == nullptr || reps == nullptr || base_out == nullptr || maa_out == nullptr ||
        maa_loop_out == nullptr) {
        std::cout << "Allocation failed" << std::endl;
        free(backing);
        free(starts);
        free(reps);
        free(base_out);
        free(maa_out);
        free(maa_loop_out);
#ifdef GEM5
        m5_exit(1);
#endif
        return 1;
    }

    init_index_chains(backing, backing_elems, starts, reps, n, depth, scenario, arena_mult);
    for (int i = 0; i < n; i++) {
        base_out[i] = 0;
        maa_out[i] = 0;
        maa_loop_out[i] = 0;
    }

#if defined(FUNC)
    alloc_MAA();
#else
    if (run_maa || run_maa_loop) {
        alloc_MAA();
    }
#endif

#if defined(FUNC)
    clear_mem_region();
    m5_add_mem_region(backing, backing + backing_elems, 0);
#elif defined(GEM5)
    m5_clear_mem_region();
    m5_add_mem_region(backing, backing + backing_elems, 0);
    if (run_maa || run_maa_loop) {
        m5_add_mem_region(starts, starts + n, 1);
        m5_add_mem_region(reps, reps + n, 2);
        m5_add_mem_region(base_out, base_out + n, 3);
        m5_add_mem_region(maa_out, maa_out + n, 4);
        m5_add_mem_region(maa_loop_out, maa_loop_out + n, 5);
    }
#endif

    std::cout << "scenario=" << scenario_name(scenario)
              << " seed=" << kScenarioSeed
              << " n=" << n
              << " max_depth=" << depth
              << " backing_elems=" << backing_elems
              << " arena_mult=" << arena_mult
              << std::endl;
    print_lane_samples(backing, starts, reps, n);

    uint32_t base_sum = 0;
    uint32_t maa_sum = 0;
    uint32_t maa_loop_sum = 0;
    if (run_base) {
        indir_ld_rep_baseline(base_out, backing, starts, reps, n);
        base_sum = checksum(base_out, n);
        global_sink ^= base_sum;
        std::cout << "baseline checksum " << base_sum << std::endl;
        std::cout << "out: ";
        for (int i = 0; i < n; i++) {
            std::cout << base_out[i] << " ";
        }
        std::cout << std::endl;
    }

    if (run_maa) {
        indir_ld_rep_maa(maa_out, backing, starts, reps, n);
        maa_sum = checksum(maa_out, n);
        global_sink ^= maa_sum;
        std::cout << "maa checksum " << maa_sum << std::endl;
        std::cout << "out: ";
        for (int i = 0; i < n; i++) {
            std::cout << maa_out[i] << " ";
        }
        std::cout << std::endl;
    }

    if (run_maa_loop) {
        indir_ld_maa_loop(maa_loop_out, backing, starts, reps, n);
        maa_loop_sum = checksum(maa_loop_out, n);
        global_sink ^= maa_loop_sum;
        std::cout << "maa loop checksum " << maa_loop_sum << std::endl;
        std::cout << "out: ";
        for (int i = 0; i < n; i++) {
            std::cout << maa_loop_out[i] << " ";
        }
        std::cout << std::endl;
    }

    bool passed = true;
    if (mode == "CMP") {
        bool maa_matches = base_sum == maa_sum && compare_arrays(base_out, maa_out, n);
        bool maa_loop_matches = base_sum == maa_loop_sum && compare_arrays(base_out, maa_loop_out, n);
        if (maa_matches && maa_loop_matches) {
            std::cout << "indir_ld_rep benchmark outputs match" << std::endl;
        } else {
            passed = false;
        }
    }

    free(backing);
    free(starts);
    free(reps);
    free(base_out);
    free(maa_out);
    free(maa_loop_out);

#ifdef GEM5
    m5_exit(passed ? 0 : 1);
#endif
    return passed ? 0 : 1;
}
