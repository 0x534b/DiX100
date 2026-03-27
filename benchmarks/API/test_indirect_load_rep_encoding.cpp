#include "MAA_gem5.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

namespace {

void check_no_conditional_encoding() {
    alloc_MAA();
    init_MAA();

    auto *data = reinterpret_cast<uint64_t *>(0x12345000ULL);
    const int tc_tile = 3;
    const int ts1_tile = 5;

    maa_indirect_load_rep<uint64_t>(data, tc_tile, ts1_tile);

    const uint64_t instr0 = *INSTR_opcode_datatype_optype_tdst1_tdst2;
    const uint64_t instr1 = *INSTR_tsrc1_tsrc2_rdst1_rdst2_rsrc1_rsrc2_rsrc3_csrc;

    assert(((instr0 >> 32) & 0xFF) == static_cast<uint64_t>(OpcodeType::INDIR_LD_REP));
    assert(((instr0 >> 24) & 0xFF) == static_cast<uint64_t>(DataType::UINT64_TYPE));
    assert(((instr0 >> 8) & 0xFF) == NA_UINT8);
    assert((instr0 & 0xFF) == NA_UINT8);

    assert(((instr1 >> 56) & 0xFF) == static_cast<uint64_t>(ts1_tile));
    assert(((instr1 >> 48) & 0xFF) == static_cast<uint64_t>(ts1_tile));
    assert(((instr1 >> 24) & 0xFF) == NA_UINT8);
    assert((instr1 & 0xFF) == static_cast<uint64_t>(tc_tile));
    assert(*INSTR_baseaddr == reinterpret_cast<uint64_t>(data));
}

void check_tile_layout() {
    alloc_MAA();
    init_MAA();

    auto *data = reinterpret_cast<uint64_t *>(0xABCDEF00ULL);
    const int tc_tile = 9;
    const int ts1_tile = 11;

    maa_indirect_load_rep<uint64_t>(data, tc_tile, ts1_tile);

    const uint64_t instr0 = *INSTR_opcode_datatype_optype_tdst1_tdst2;
    const uint64_t instr1 = *INSTR_tsrc1_tsrc2_rdst1_rdst2_rsrc1_rsrc2_rsrc3_csrc;

    assert(((instr0 >> 32) & 0xFF) == static_cast<uint64_t>(OpcodeType::INDIR_LD_REP));
    assert(((instr0 >> 24) & 0xFF) == static_cast<uint64_t>(DataType::UINT64_TYPE));
    assert(((instr0 >> 8) & 0xFF) == NA_UINT8);

    assert(((instr1 >> 56) & 0xFF) == static_cast<uint64_t>(ts1_tile));
    assert(((instr1 >> 48) & 0xFF) == static_cast<uint64_t>(ts1_tile));
    assert((instr1 & 0xFF) == static_cast<uint64_t>(tc_tile));
    assert(*INSTR_baseaddr == reinterpret_cast<uint64_t>(data));
}

} // namespace

int main() {
    check_no_conditional_encoding();
    check_tile_layout();
    std::cout << "INDIR_LD_REP encoding test passed" << std::endl;
    return 0;
}
