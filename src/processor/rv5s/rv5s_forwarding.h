/**
 * @file rv5s_forwarding.h
 * @brief Forwarding an INTEGER register into an FP instruction's rs1.
 *
 * `fcvt.{s,d}.{w,wu,l,lu}`, `fmv.{w,d}.x` and the base address of
 * `flw`/`fld`/`fsw`/`fsd` read an x register. The FP execute path used
 * to take that value straight from the architectural register file -
 * after the forwarding mux, by an explicit "re-apply GPR-read override"
 * that would have discarded a forwarded value even if one had existed -
 * so on the forwarding variants a producer still in flight was not seen
 * and `li t0,3; fcvt.d.l f1,t0` gave f1 = 0 (known_issues 76).
 *
 * One helper, used by every such site in the two forwarding variants,
 * so the semantics cannot drift from the integer path's. It is the same
 * selection and the same priority as `rv5s_processor_h_f.cpp`'s GPR
 * block uses for an ordinary instruction's rs1:
 *
 *     1. EX/MEM   if it writes a GPR and rd != 0 and rd == rs1
 *     2. MEM/WB   the same, only if 1 did not fire
 *     3. the register file
 *
 * Step 3 differs from the integer path's `id_ex_reg_.reg1_data` because
 * decode never latches reg1_data for an F/D instruction
 * (rv5s_processor_base.cpp takes the FP branch and reads only
 * frs1/frs2/frs3), so the caller passes the register-file value in.
 * That is also why the two models WITHOUT forwarding keep reading the
 * register file directly and are untouched: h_nf stalls until the value
 * is architectural, and nh_nf has no hazard handling at all by
 * contract.
 */
#ifndef KITES_RV5S_FORWARDING_H
#define KITES_RV5S_FORWARDING_H

#include "common/instructions.h"
#include "processor/pipeline_registers.h"

#include <cstdint>

namespace Kites
{

/**
 * @brief Does this producer write an INTEGER register?
 *
 * `reg_write` alone cannot answer it. The control unit sets `reg_write`
 * for every OP-FP instruction, and the EX/MEM `rd` field carries the
 * same five bits as `frd`, so without this guard `fadd.d f5, …` would
 * forward its floating-point result into a consumer reading x5.
 *
 * The answer mirrors the writeback decision in
 * rv5s_processor_base.cpp's pipeline_writeback_float/double: an F/D
 * instruction writes a GPR only for the compare, float-to-integer,
 * classify and move-out rows.
 */
inline bool producerWritesGpr(uint32_t instruction, bool reg_write)
{
    if (!reg_write)
    {
        return false;
    }
    if (!instruction_set::isFInstruction(instruction) &&
        !instruction_set::isDInstruction(instruction))
    {
        return true; // an ordinary integer instruction
    }
    const uint8_t opcode = instruction & 0b1111111;
    if (opcode != 0b1010011)
    {
        return false; // an FP load, store or FMA: it writes an f register
    }
    const uint8_t funct7 = (instruction >> 25) & 0b1111111;
    // feq/flt/fle, fcvt.{w,wu,l,lu}.{s,d}, fclass and fmv.x.{w,d}
    return (funct7 & ~1u) == 0b1010000 || (funct7 & ~1u) == 0b1100000 ||
           (funct7 & ~1u) == 0b1110000;
}

/**
 * @brief Does this producer write an F register?
 *
 * The mirror of producerWritesGpr, and needed for the same reason from
 * the other side: `freg_write` is set for every F/D instruction, and the
 * EX/MEM `frd` field carries the same five bits as `rd`. Without this
 * guard `fcvt.l.d t0, f6` - which writes the INTEGER register t0 -
 * forwards its integer result into a consumer reading f5, because t0 is
 * x5 and the indices match. Measured on both forwarding variants
 * (known_issues 76).
 */
inline bool producerWritesFpr(uint32_t instruction, bool freg_write)
{
    if (!freg_write)
    {
        return false;
    }
    const uint8_t opcode = instruction & 0b1111111;
    if (opcode != 0b1010011)
    {
        return true; // an FP load, store or FMA writes an f register
    }
    const uint8_t funct7 = (instruction >> 25) & 0b1111111;
    // everything but the compare, float-to-integer, classify and move-out rows
    return !((funct7 & ~1u) == 0b1010000 || (funct7 & ~1u) == 0b1100000 ||
             (funct7 & ~1u) == 0b1110000);
}

/**
 * @brief The value of x[rs1] as this instruction should see it.
 * @param fallback what the architectural register file holds, used when
 *                 no producer is in flight.
 */
inline uint64_t forwardedIntegerSource(uint8_t rs1, const EX_MEM_Register &ex_mem,
                                       const MEM_WB_Register &mem_wb, uint64_t fallback)
{
    if (rs1 == 0)
    {
        return 0; // x0 is hardwired; nothing forwards to it or from it
    }
    if (ex_mem.rd == rs1 && ex_mem.rd != 0 &&
        producerWritesGpr(ex_mem.instruction, ex_mem.reg_write))
    {
        return ex_mem.alu_result;
    }
    if (mem_wb.prev_rd == rs1 && mem_wb.prev_rd != 0 &&
        producerWritesGpr(mem_wb.prev_instruction, mem_wb.prev_reg_write))
    {
        return mem_wb.prev_mem_to_reg ? mem_wb.prev_memory_data : mem_wb.prev_alu_result;
    }
    return fallback;
}

} // namespace Kites

#endif // KITES_RV5S_FORWARDING_H
