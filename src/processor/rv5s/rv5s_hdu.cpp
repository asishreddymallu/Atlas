/**
 * @file hazard_detection_unit.cpp
 * @brief Logic for the Hazard Detection Unit (HDU).
 * * This function determines if a data hazard exists between instructions in
 * the ID/EX and IF/ID pipeline registers, and calculates the number of stalls
 * (bubbles) needed based on the simulator's mode (Forwarding ON/OFF).
 * * NOTE: This version includes and uses the full structs from pipeline_registers.h.
 */
#include <algorithm>
#include <cstdint>
#include <iostream>


#include "common/instructions.h"
#include "processor/pipeline_registers.h"
#include "processor/rv5s/rv5s_forwarding.h"

namespace Kites
{
// Define stall count constants
constexpr int STALL_NONE = 0;
constexpr int STALL_ONE_CYCLE = 1;
constexpr int STALL_TWO_CYCLES = 2;

/**
 * @brief Checks for data hazards and returns the number of stalls required.
 * @param if_id_reg The register holding the instruction in the IF/ID stage (the dependent
 * instruction).
 * @param id_ex_reg The register holding the instruction in the ID/EX stage (the source
 * instruction).
 * @param is_forwarding_enabled Flag to switch between 'Forwarding ON' and 'No Forwarding' modes.
 * @return The number of cycles the pipeline should stall (0, 1, or 2).
 */
int check_data_hazard(const IF_ID_Register &if_id_reg, const ID_EX_Register &id_ex_reg,
                      const EX_MEM_Register &ex_mem_reg, bool is_forwarding_enabled)
{
    // --- 1. What the IF/ID instruction really reads ---
    // Only the fields it reads, each in its own file. The bare bits of every
    // instruction used to be compared against both files, so an f5 producer
    // stalled a consumer reading x5, and lui/auipc/jal stalled on their
    // immediate bits (known_issues 78, 105).
    const uint32_t instruction = if_id_reg.instruction;
    const instruction_set::SourceRegisters src = instruction_set::sourceRegisters(instruction);
    const uint8_t rs1 = (instruction >> 15) & 0x1F;
    const uint8_t rs2 = (instruction >> 20) & 0x1F;
    const uint8_t rs3 = (instruction >> 27) & 0x1F;
    auto readsGpr = [&](uint8_t rd)
    { return rd != 0 && ((src.rs1_gpr && rd == rs1) || (src.rs2_gpr && rd == rs2)); };
    auto readsFpr = [&](uint8_t frd)
    {
        return (src.rs1_fpr && frd == rs1) || (src.rs2_fpr && frd == rs2) ||
               (src.rs3_fpr && frd == rs3);
    };

    // --- 2./3. Producers in flight, judged by the file they really write ---
    // reg_write and freg_write are both set for F/D instructions (79), so the
    // instruction decides (producerWritesGpr / producerWritesFpr).
    const bool gpr_hazard_two_cycle =
        producerWritesGpr(ex_mem_reg.instruction, ex_mem_reg.reg_write) && readsGpr(ex_mem_reg.rd);
    const bool gpr_hazard_one_cycle =
        producerWritesGpr(ex_mem_reg.prev_instruction, ex_mem_reg.prev_reg_write) &&
        readsGpr(ex_mem_reg.prev_rd);
    const bool fpr_hazard_two_cycle =
        producerWritesFpr(ex_mem_reg.instruction, ex_mem_reg.freg_write) && readsFpr(ex_mem_reg.frd);
    const bool fpr_hazard_one_cycle =
        producerWritesFpr(ex_mem_reg.prev_instruction, ex_mem_reg.prev_freg_write) &&
        readsFpr(ex_mem_reg.prev_frd);
    const bool gpr_hazard = gpr_hazard_one_cycle || gpr_hazard_two_cycle;
    const bool fpr_hazard = fpr_hazard_one_cycle || fpr_hazard_two_cycle;

    // --- 4. Determine Stall Count ---
    // Combined hazard flag (either GPR or FPR dependency)
    bool any_hazard_two_cycle = gpr_hazard_two_cycle || fpr_hazard_two_cycle;
    bool any_hazard_one_cycle = gpr_hazard_one_cycle || fpr_hazard_one_cycle;
    bool any_hazard = gpr_hazard || fpr_hazard;

    if (is_forwarding_enabled)
    {
        // --- Forwarding ON: Detects Load-Use only (1 Stall) ---
        // Load-Use hazard occurs if the producing instruction (ID/EX) is a Load
        // AND the dependent instruction reads that destination register.
        if (id_ex_reg.mem_read)
        {
            // Check if the load destination matches any source of the dependent instruction
            const bool gpr_load_use =
                producerWritesGpr(id_ex_reg.instruction, id_ex_reg.reg_write) && readsGpr(id_ex_reg.rd);
            const bool fpr_load_use =
                producerWritesFpr(id_ex_reg.instruction, id_ex_reg.freg_write) && readsFpr(id_ex_reg.frd);

            if (gpr_load_use || fpr_load_use)
            {
                return STALL_ONE_CYCLE;
            }
        }

        // All other hazards (R-R, F-F, R-Store, F-Store) are solved by the forwarding unit.
        return STALL_NONE;
    }
    else if (!any_hazard)
    {
        return STALL_NONE;
    }
    else if (any_hazard_two_cycle)
    {
        return STALL_TWO_CYCLES;
    }
    else if (any_hazard_one_cycle)
    {
        return STALL_ONE_CYCLE;
    }

    return STALL_NONE;
}
}//namespace Kites