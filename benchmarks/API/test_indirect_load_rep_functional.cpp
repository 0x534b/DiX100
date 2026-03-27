#include "MAA_functional.hpp"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {

constexpr int ARR_SZ = 64;
constexpr uint32_t INDIRECTION_LEVEL = 4;

void initialize_pointer_chain(uint64_t *rep_arr, uint32_t *counts) {
    std::srand(7);
    for (int i = 0; i < ARR_SZ; i++) {
        rep_arr[i] = reinterpret_cast<uint64_t>(&rep_arr[std::rand() % ARR_SZ]);
        counts[i] = INDIRECTION_LEVEL;
    }
}

void compute_expected(const uint64_t *rep_arr, const uint32_t *counts, uint64_t *expected) {
    for (int i = 0; i < ARR_SZ; i++) {
        uint64_t current = rep_arr[i];
        for (uint32_t depth = 0; depth < counts[i]; depth++) {
            current = *reinterpret_cast<const uint64_t *>(current);
        }
        expected[i] = current;
    }
}

void check_repeated_dereference() {
    alloc_MAA();
    init_MAA();
    clear_mem_region();

    uint64_t rep_arr[ARR_SZ];
    uint32_t counts[ARR_SZ];
    uint64_t expected[ARR_SZ];
    uint64_t observed[ARR_SZ];

    initialize_pointer_chain(rep_arr, counts);
    compute_expected(rep_arr, counts, expected);

    m5_add_mem_region(rep_arr, rep_arr + ARR_SZ, 0);
    m5_add_mem_region(counts, counts + ARR_SZ, 1);

    const int min_reg = get_new_reg<int>(0);
    const int max_reg = get_new_reg<int>(ARR_SZ);
    const int stride_reg = get_new_reg<int>(1);
    const int tc_tile = get_new_tile<uint32_t>();
    const int ts1_tile = get_new_tile<uint64_t>();

    maa_stream_load<uint32_t>(counts, min_reg, max_reg, stride_reg, tc_tile);
    maa_stream_load<uint64_t>(rep_arr, min_reg, max_reg, stride_reg, ts1_tile);
    maa_indirect_load_rep<uint64_t>(rep_arr, tc_tile, ts1_tile);
    wait_ready(ts1_tile);

    auto *ts1 = get_cacheable_tile_pointer<uint64_t>(ts1_tile);
    assert(get_tile_size(tc_tile) == ARR_SZ);
    assert(get_tile_size(ts1_tile) == ARR_SZ);
    for (int i = 0; i < ARR_SZ; i++) {
        observed[i] = ts1[i];
        assert(observed[i] == expected[i]);
    }
}

} // namespace

int main() {
    check_repeated_dereference();
    std::cout << "INDIR_LD_REP functional test passed" << std::endl;
    return 0;
}
