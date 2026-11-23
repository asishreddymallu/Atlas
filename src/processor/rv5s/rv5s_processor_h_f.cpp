/**
 * @file rv5s_processor_h_f.cpp
 * @brief Implementation for the 5-stage pipelined VM (RV5S) in Mode 4: Hazard Detection, With
 * Forwarding.
 * * NOTE: This is a PURE INTEGER (GPR) implementation.
 * * Stall Rule: Only Load-Use GPR dependencies require a 1-cycle stall. All others are solved by
 * forwarding.
 * * Control Hazards (JAL/JALR, B-Type) are AUTOMATICALLY handled by flush/redirect.
 * @author Atharva and Harshit
 */
#include "processor/rv5s/rv5s_forwarding.h"
#include "processor/rv5s/rv5s_processor_h_f.h" // Assuming this header now defines RV5StageProcessorHF
#include "common/instructions.h"
#include "config/config.h"
#include "ui/processor_tab/processor_designs/rv5s_processor_h_f_circuit_scene.h"
#include "processor/alu.h"
#include "processor/pipeline_registers.h"
#include "processor/processor_base.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <tuple>

namespace Atlas
{
using namespace alu;

// Define stall count constants (used internally, defined in rv5s_hdu.h)
// constexpr int STALL_NONE = 0;
// constexpr int STALL_ONE_CYCLE = 1;

// --- RV5StageProcessorHF Class Implementation ---

RV5StageProcessorHF::RV5StageProcessorHF() : RV5StageVM_Base()
{
    // Initialize ProcessorBase members
    // program_counter_ = 0;
    // instructions_retired_ = 0;
    // cycle_s_ = 0;
    // stall_cycles_ = 0;

    // // Initialize local members
    // stall_fetch_and_decode_ = false;

    // Reset components and history
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Atlas::RV5StageVM_H_F_CircuitScene>();
    connect(this, &ProcessorBase::circuitSnapshotSignal, circuit_scene_.get(),
            &Atlas::RV5StageVM_H_F_CircuitScene::updateCircuitSnapshot);
#endif
    Reset();

    active_wires_.append("PC_to_IM");
    active_wires_.append("IM_to_P1");
    active_wires_.append("P1_to_P2_PCcarry");
    active_wires_.append("P1_to_Control_RF_andall");
    active_wires_.append("RF_to_P2_UP");
    active_wires_.append("RFdown_to_P2");
    active_wires_.append("PCMux_to_PC");
    active_wires_.append("P2_to_ALU2");
    active_wires_.append("P2_to_P3_MEMControl");
    active_wires_.append("P2_to_P3_WBcontrol");
    active_wires_.append("ALU2_to_P3");
    active_wires_.append("P3_to_P4_WBcontrol");
    active_wires_.append("P2_to_ALUControl");
    active_wires_.append("RdP4_to_FU");
    active_wires_.append("P4EB_to_FWD_unit");
    active_wires_.append("P2_to_FDU_rs1");
    active_wires_.append("P2_to_FDU_RS2");
    active_wires_.append("ALU_to_P3");
    active_wires_.append("P3ALUres_to_DMup");
    active_wires_.append("ALUcontrol_to_ALU");
    active_wires_.append("Fmux1_to_ALU");
    active_wires_.append("Fmux_to_ALUMux_up");
    active_wires_.append("P2_to_FMUX");
    active_wires_.append("P2_to_FMUX2_UP");
    active_wires_.append("P2_to_ALUMux");
    active_wires_.append("P2_to_ALUcontrol_Control");

    always_active_wires_count_ = active_wires_.size();
}

void RV5StageProcessorHF::SetActiveWireNames()
{
    // Clear and populate canonical wires for the Hazard+Forwarding (H_F) circuit
    resetWiresToBackbone();
    // Backbone / always-visible wires (names sourced from H_F_Processor.json)

    // Every condition below reads the register holding the instruction its
    // stage label shows after this Step (setProcessorState): ID = if_id_reg_,
    // EX = id_ex_reg_, MEM = ex_mem_reg_, WB = mem_wb_reg_. Never a prev_*
    // shadow - those are one generation old here (known_issues 95).

    // ID: the immediate generator's output into ID/EX
    if (idStageControl().GetAluSrc())
    {
        lightWire("Imm_to_P2", WireStage::ID);
    }

    // EX: immediate selected as the ALU's second operand
    if (id_ex_reg_.alu_src)
    {
        lightWire("P2Imm_to_ALU2_down", WireStage::EX);
    }

    // EX: control carried on from ID/EX to EX/MEM
    if (id_ex_reg_.mem_write)
    {
        lightWire("P2_to_P3_MEMControl", WireStage::EX);
    }
    if (id_ex_reg_.reg_write)
    {
        lightWire("P2_to_P3_WBcontrol", WireStage::EX);
    }

    // MEM: memory control / data path
    if (ex_mem_reg_.mem_read)
    {
        lightWire("P3_to_DM_Memread", WireStage::MEM);
        lightWire("DMMux_to_DM", WireStage::MEM);
    }
    if (ex_mem_reg_.mem_write)
    {
        lightWire("P3_TO_DM_control_memwrite", WireStage::MEM);
        lightWire("P3ALUres_to_DMup", WireStage::MEM);
    }

    // WB: register writeback paths
    if (mem_wb_reg_.reg_write)
    {
        lightWire("P4_wbcontrol_to_WBmux", WireStage::WB);
        lightWire("RdP4_to_FU", WireStage::WB);
        lightWire("WBMux_to_RF", WireStage::WB);
    }

    // Forwarding unit activity: show FWD wires when forwarding is relevant
    if (producerWritesGpr(ex_mem_reg_.instruction, ex_mem_reg_.reg_write) &&
        ex_mem_reg_.rd != 0)
    {
        // producerWritesGpr, not reg_write: the control unit sets reg_write
        // for every OP-FP instruction and the EX/MEM rd field carries the
        // same five bits as frd, so the bare test lit the integer
        // forwarding path for `fadd.d f5, ...` followed by anything that
        // reads x5 - a path with nothing travelling along it. The same
        // substitution 79 made in the datapath (known_issues 81).
        if (ex_mem_reg_.rd == id_ex_reg_.rs1)
        {
            lightWire("P2_to_FDU_rs1", WireStage::EX);
            lightWire("P4EB_to_FWD_unit", WireStage::EX);
            lightWire("FDU_to_FMUX1_cntrl", WireStage::EX);
            lightWire("FDU_to_fMUX2", WireStage::EX);
        }
        if (ex_mem_reg_.rd == id_ex_reg_.rs2)
        {
            lightWire("P2_to_FDU_RS2", WireStage::EX);
            lightWire("P4EB_to_FWD_unit", WireStage::EX);
            lightWire("FDU_to_FMUX3", WireStage::EX);
        }
    }

    // The same guard one stage further down the pipeline. mem_wb_reg_, not
    // its prev_* copy: pipeline_memory() copies exactly these fields into
    // prev_* at the start of the next Step, which is what the EX of the
    // instruction now in ID/EX forwards from (known_issues 95).
    if (producerWritesGpr(mem_wb_reg_.instruction, mem_wb_reg_.reg_write) &&
        mem_wb_reg_.rd != 0)
    {
        if (mem_wb_reg_.rd == id_ex_reg_.rs1)
        {
            lightWire("P4EB_to_FWD_unit", WireStage::EX);
            lightWire("FDU_to_FMUX1_cntrl", WireStage::EX);
        }
        if (mem_wb_reg_.rd == id_ex_reg_.rs2)
        {
            lightWire("P4EB_to_FWD_unit", WireStage::EX);
            lightWire("FDU_to_FMUX3", WireStage::EX);
        }
    }

    // EX: the comparison result of a branch, taken or not
    if (isBranchInstruction(id_ex_reg_.instruction))
    {
        lightWire("ALU_zerores_to_P3", WireStage::EX);
    }

    // PC selection. A taken branch in MEM selects its target through the
    // AND gate; a jump redirects from EX along a path this drawing does not
    // have, so in that cycle neither input of the PC mux lights.
    if (branchRedirectsNextStep())
    {
        lightWire("ANDGate_lower_entry", WireStage::MEM);
        lightWire("ANDGATE_to_PCMUX", WireStage::MEM);
        lightWire("P3_to_PCMux", WireStage::MEM);
    }
    else if (!jumpRedirectsNextStep())
    {
        lightWire("ALU1_to_PCMuxUp", WireStage::IF);
    }

    // ID: the hazard unit stalls this cycle - the decision Step() makes next
    if (hazardUnitWillStall(true))
    {
        lightWire("HDU_to_P1", WireStage::ID);
        lightWire("HDUMux_to_P2", WireStage::ID);
        lightWire("HDU_mux_lowest_toP2", WireStage::ID);
        lightWire("HDU_to_HDU_mUX", WireStage::ID);
    }

    // lightWire keeps the list free of duplicates, first occurrence first.

    // The scene will read `active_wires_` when `updateCircuitStateSignal` is emitted by callers.
}

void RV5StageProcessorHF::Reset()
{
    RV5StageVM_Base::Reset();
    stall_fetch_and_decode_ = false;
}

void RV5StageProcessorHF::Step()
{
    begin_step_delta();

    // 1. Execute back stages (WB -> MEM -> EX)
    pipeline_writeback();
    pipeline_memory();
    pipeline_execute();

    // --- Hazard Detection and Stall Logic (The core of Mode 4) ---

    int stalls_needed = 0;
    bool currently_stalling = (stall_cycles_ > 0);

    if (currently_stalling)
    {
        // Stall is already active. Decrement counter and continue stall.
        stall_cycles_--;
        stalls_needed = 1; // Indicate we are continuing the stall (1 cycle consumed)
    }
    else
    {
        // Pipeline is free. Check for a NEW hazard using the external HDU function.
        stalls_needed = check_data_hazard(if_id_reg_, id_ex_reg_, ex_mem_reg_,
                                          true /* is_forwarding_enabled */);
        if (stalls_needed > 0)
        {
            // Start the stall: stalls_needed cycles total. 1 cycle is handled now.
            stall_cycles_ = stalls_needed - 1;
        }
    }

    // 2. Process Decode/Fetch based on stall status
    if (stalls_needed > 0)
    {
        // STALL: Freeze IF/ID (by preventing its update) and inject NOP into ID/EX
        id_ex_reg_.reset();
        ++total_stall_cycles_;
        // Skip pipeline_decode() - The instruction in IF/ID stays put.
        stall_fetch_and_decode_ = true;
    }
    else
    {
        // NO STALL: Advance pipeline normally
        pipeline_decode();
        stall_fetch_and_decode_ = false;
    }

    // 3. Fetch and PC update
    fetch_and_advance_pc(stall_fetch_and_decode_);

    cycle_s_++; // One clock cycle has passed

    finalize_step_delta();
}

// --- Pipeline Stage Implementations (Mode 4: H_F) ---

void RV5StageProcessorHF::pipeline_fetch()
{
    // If a stall is active, we prevent IF/ID from updating.
    if (stall_fetch_and_decode_)
    {
        // IF/ID register is intentionally NOT updated, holding the stalled instruction.
        return;
    }

    if (program_counter_ < program_size_)
    {
        if_id_reg_.instruction = memory_controller_.readInstruction(program_counter_);
        if_id_reg_.pc = program_counter_;
    }
    else
    {
        if_id_reg_.reset();
    }
}

void RV5StageProcessorHF::pipeline_execute()
{
    uint32_t instruction = id_ex_reg_.instruction;
    bool overflow;
    uint64_t alu_result;

    bool is_f_instruction = instruction_set::isFInstruction(instruction);
    bool is_d_instruction = instruction_set::isDInstruction(instruction);

    if (is_f_instruction)
    {
        alu_result = pipeline_execute_float();
    }
    else if (is_d_instruction)
    {
        alu_result = pipeline_execute_double();
    }
    else
    {
        uint64_t alu_in1 = id_ex_reg_.reg1_data;
        uint64_t alu_in2 = id_ex_reg_.reg2_data;

        // --- GPR FORWARDING LOGIC ---
        uint8_t forward_a = 0; // 10 = EX/MEM, 01 = MEM/WB
        uint8_t forward_b = 0;

        // **Priority 1: EX/MEM Forward** (R-R, R-Store hazard solved)
        //
        // producerWritesGpr, not reg_write: the control unit sets
        // reg_write for every OP-FP instruction and rd carries the same
        // five bits as frd, so `fadd.d f5, …` would otherwise forward
        // its floating-point result into an ordinary `addi` reading x5
        // (known_issues 79).
        if (producerWritesGpr(ex_mem_reg_.instruction, ex_mem_reg_.reg_write) &&
            (ex_mem_reg_.rd != 0))
        {
            if (ex_mem_reg_.rd == id_ex_reg_.rs1)
                forward_a = 2;
            if (ex_mem_reg_.rd == id_ex_reg_.rs2)
                forward_b = 2;
        }

        // **Priority 2: MEM/WB Forward** (R-R, L-Store hazard solved)
        if (producerWritesGpr(mem_wb_reg_.prev_instruction, mem_wb_reg_.prev_reg_write) &&
            (mem_wb_reg_.prev_rd != 0))
        {
            // Forward A from MEM/WB unless EX/MEM is already forwarding
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs1 && forward_a != 2)
                forward_a = 1;
            // Forward B from MEM/WB unless EX/MEM is already forwarding
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs2 && forward_b != 2)
                forward_b = 1;
        }

        // --- FORWARDING APPLICATION ---

        if (forward_a == 2)
        {
            alu_in1 = ex_mem_reg_.alu_result;
        }
        else if (forward_a == 1)
        {
            alu_in1 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        if (forward_b == 2)
        {
            alu_in2 = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        {
            alu_in2 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        // Re-apply immediate value check (must happen AFTER forwarding application)
        if (id_ex_reg_.alu_src)
        {
            alu_in2 = static_cast<uint64_t>(id_ex_reg_.imm);
        }

        // --- EXECUTION ---
        alu::AluOp alu_operation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
        std::tie(alu_result, overflow) = alu::Alu::execute(alu_operation, alu_in1, alu_in2);
        // temporary fix for lui instruction

        if ((id_ex_reg_.instruction & 0b1111111) == 0b0110111) // lui
        {
            alu_result = static_cast<uint64_t>(id_ex_reg_.imm << 12);
        }
        if ((id_ex_reg_.instruction & 0b1111111) == 0b0010111) // auipc (known_issues 103)
        {
            alu_result = auipc_result(id_ex_reg_);
        }

        // Latch data for EX/MEM Register

        // Store Data: CRITICAL FORWARDING - Store data must also be forwarded!
        uint64_t store_data = id_ex_reg_.reg2_data;
        if (forward_b == 2)
        {
            store_data = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        {
            store_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                     : mem_wb_reg_.prev_alu_result;
        }

        ex_mem_reg_.reg2_data = store_data;
    }

    ex_mem_reg_.prev_reg_write = ex_mem_reg_.reg_write;
    ex_mem_reg_.prev_rd = ex_mem_reg_.rd;
    ex_mem_reg_.prev_instruction = ex_mem_reg_.instruction;
    ex_mem_reg_.prev_freg_write = ex_mem_reg_.freg_write;
    ex_mem_reg_.prev_frd = ex_mem_reg_.frd;
    ex_mem_reg_.pc = id_ex_reg_.pc;
    ex_mem_reg_.instruction = id_ex_reg_.instruction;
    ex_mem_reg_.fp_status = ex_fp_status_valid_ ? ex_fp_status_ : 0;
    ex_mem_reg_.writes_fp_status = ex_fp_status_valid_;
    ex_fp_status_valid_ = false;
    ex_mem_reg_.alu_result = alu_result;
    ex_mem_reg_.f_alu_result = (is_f_instruction || is_d_instruction) ? alu_result : 0;
    ex_mem_reg_.rd = id_ex_reg_.rd;
    ex_mem_reg_.frd = id_ex_reg_.frd;
    // NOTE: reg2_data and freg2_data are set inside the integer/FP execute
    // branches above with proper forwarding applied — do NOT overwrite here.
    ex_mem_reg_.reg_write = id_ex_reg_.reg_write;
    ex_mem_reg_.freg_write = id_ex_reg_.freg_write;
    ex_mem_reg_.mem_to_reg = id_ex_reg_.mem_to_reg;
    ex_mem_reg_.prev_mem_read = ex_mem_reg_.mem_read;
    ex_mem_reg_.prev_mem_write = ex_mem_reg_.mem_write;
    ex_mem_reg_.mem_read = id_ex_reg_.mem_read;
    ex_mem_reg_.mem_write = id_ex_reg_.mem_write;
    ex_mem_reg_.prev_branch_taken = ex_mem_reg_.branch_taken;
    ex_mem_reg_.branch_taken = false;
    ex_mem_reg_.branch_target_pc = 0;

    uint8_t opcode = instruction & 0b1111111;

    // --- Control Hazard Logic (Automated) ---

    // Conditional Branch Check (B-type)
    if (id_ex_reg_.branch && opcode == 0b1100011)
    {
        bool condition_met = false;
        uint8_t funct3 = (instruction >> 12) & 0x7;

        switch (funct3)
        {
        case 0b000:
            condition_met = (alu_result == 0);
            break; // BEQ
        case 0b001:
            condition_met = (alu_result != 0);
            break; // BNE
        case 0b100:
            condition_met = (alu_result == 1);
            break; // BLT
        case 0b101:
            condition_met = (alu_result == 0);
            break; // BGE
        case 0b110:
            condition_met = (alu_result == 1);
            break; // BLTU
        case 0b111:
            condition_met = (alu_result == 0);
            break; // BGEU
        }

        if (condition_met)
        {
            ex_mem_reg_.branch_taken = true;
            ex_mem_reg_.branch_target_pc = id_ex_reg_.pc + id_ex_reg_.imm;
        }
    }
    // Unconditional Jump Check (JAL/JALR: 1-cycle penalty)
    else if (opcode == 0b1101111 || opcode == 0b1100111)
    {
        uint64_t jump_target;
        if (opcode == 0b1101111)
        {
            jump_target = id_ex_reg_.pc + id_ex_reg_.imm; // JAL
        }
        else
        {
            jump_target = alu_result & ~1;              // JALR (ALU result is Reg + Imm)
            ex_mem_reg_.alu_result = id_ex_reg_.pc + 4; // Set link address (PC+4)
        }

        redirect_pc(jump_target);
        if_id_reg_.reset();
        ex_mem_reg_.branch_taken = true;
    }
}


uint64_t RV5StageProcessorHF::pipeline_execute_float()
{
    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t rm = funct3;

    uint8_t fcsr_status = 0;
    uint64_t alu_result = 0;

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = id_ex_reg_.freg1_data;
    uint64_t reg2_value = id_ex_reg_.freg2_data;
    uint64_t reg3_value = id_ex_reg_.freg3_data;

    if (instruction_set::readsIntegerSource(instruction))
    {
        // The integer source, forwarded by the same rule the integer
        // path uses for its own rs1. It used to be a bare ReadGpr here,
        // and a second one after the forwarding mux that would have
        // discarded a forwarded value (known_issues 76).
        reg1_value = forwardedIntegerSource(id_ex_reg_.rs1, ex_mem_reg_, mem_wb_reg_,
                                            registers_.ReadGpr(id_ex_reg_.rs1));
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // --- FORWARDING for FPR sources (frs1/frs2/frs3) ---
    uint8_t fwd_a = 0; // 2 = EX/MEM, 1 = MEM/WB
    uint8_t fwd_b = 0;
    uint8_t fwd_c = 0;

    // EX/MEM forwarding (highest priority)
    if (producerWritesFpr(ex_mem_reg_.instruction, ex_mem_reg_.freg_write) &&
        (ex_mem_reg_.frd != 0))
    {
        if (ex_mem_reg_.frd == id_ex_reg_.frs1)
            fwd_a = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs2)
            fwd_b = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs3)
            fwd_c = 2;
    }

    // MEM/WB forwarding (lower priority)
    if (producerWritesFpr(mem_wb_reg_.prev_instruction, mem_wb_reg_.prev_freg_write) &&
        (mem_wb_reg_.prev_frd != 0))
    {
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs1 && fwd_a != 2)
            fwd_a = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs2 && fwd_b != 2)
            fwd_b = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs3 && fwd_c != 2)
            fwd_c = 1;
    }

    // Apply forwarding to source values.
    //
    // fwd_a is skipped when rs1 is an INTEGER register: frs1 and rs1 are
    // the same five bits, so an f register with that index would
    // otherwise forward its value into x[rs1]. Measured:
    // `fcvt.d.l f5, t1` then `fcvt.d.l f1, t0` with t0 = x5 converted
    // f5's bit pattern as an integer (known_issues 76).
    if (!instruction_set::readsIntegerSource(instruction))
    {
        if (fwd_a == 2)
            reg1_value = ex_mem_reg_.f_alu_result;
        else if (fwd_a == 1)
            reg1_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                     : mem_wb_reg_.prev_f_alu_result;
    }

    if (fwd_b == 2)
        reg2_value = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        reg2_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_c == 2)
        reg3_value = ex_mem_reg_.f_alu_result;
    else if (fwd_c == 1)
        reg3_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;


    // Re-apply immediate if ALUSrc was set (immediates take precedence for reg2)
    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // Forward store data for FPR stores (frs2)
    uint64_t fstore_data = id_ex_reg_.freg2_data;
    if (fwd_b == 2)
        fstore_data = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        fstore_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                  : mem_wb_reg_.prev_f_alu_result;

    ex_mem_reg_.freg2_data = fstore_data;
    // Execute FP operation
    alu::AluOp aluOperation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    latch_fp_status(fcsr_status);

    return alu_result;
}

uint64_t RV5StageProcessorHF::pipeline_execute_double()
{
    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t funct3 = (instruction >> 12) & 0b111;
    uint8_t rm = funct3;

    uint8_t fcsr_status = 0;
    uint64_t alu_result = 0;

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = id_ex_reg_.freg1_data;
    uint64_t reg2_value = id_ex_reg_.freg2_data;
    uint64_t reg3_value = id_ex_reg_.freg3_data;

    if (instruction_set::readsIntegerSource(instruction))
    {
        // The integer source, forwarded by the same rule the integer
        // path uses for its own rs1. It used to be a bare ReadGpr here,
        // and a second one after the forwarding mux that would have
        // discarded a forwarded value (known_issues 76).
        reg1_value = forwardedIntegerSource(id_ex_reg_.rs1, ex_mem_reg_, mem_wb_reg_,
                                            registers_.ReadGpr(id_ex_reg_.rs1));
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // --- FORWARDING for FPR sources (frs1/frs2/frs3) ---
    uint8_t fwd_a = 0; // 2 = EX/MEM, 1 = MEM/WB
    uint8_t fwd_b = 0;
    uint8_t fwd_c = 0;

    if (producerWritesFpr(ex_mem_reg_.instruction, ex_mem_reg_.freg_write) &&
        (ex_mem_reg_.frd != 0))
    {
        if (ex_mem_reg_.frd == id_ex_reg_.frs1)
            fwd_a = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs2)
            fwd_b = 2;
        if (ex_mem_reg_.frd == id_ex_reg_.frs3)
            fwd_c = 2;
    }

    if (producerWritesFpr(mem_wb_reg_.prev_instruction, mem_wb_reg_.prev_freg_write) &&
        (mem_wb_reg_.prev_frd != 0))
    {
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs1 && fwd_a != 2)
            fwd_a = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs2 && fwd_b != 2)
            fwd_b = 1;
        if (mem_wb_reg_.prev_frd == id_ex_reg_.frs3 && fwd_c != 2)
            fwd_c = 1;
    }

    // Skipped when rs1 is an integer register; see the float path above.
    if (!instruction_set::readsIntegerSource(instruction))
    {
        if (fwd_a == 2)
            reg1_value = ex_mem_reg_.f_alu_result;
        else if (fwd_a == 1)
            reg1_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                     : mem_wb_reg_.prev_f_alu_result;
    }

    if (fwd_b == 2)
        reg2_value = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        reg2_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;

    if (fwd_c == 2)
        reg3_value = ex_mem_reg_.f_alu_result;
    else if (fwd_c == 1)
        reg3_value = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                 : mem_wb_reg_.prev_f_alu_result;


    // Re-apply immediate for reg2
    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    // Forward store data for FPR stores
    uint64_t fstore_data = id_ex_reg_.freg2_data;
    if (fwd_b == 2)
        fstore_data = ex_mem_reg_.f_alu_result;
    else if (fwd_b == 1)
        fstore_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_f_memory_data
                                                  : mem_wb_reg_.prev_f_alu_result;

    ex_mem_reg_.freg2_data = fstore_data;

    alu::AluOp alu_operation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::dfpexecute(alu_operation, reg1_value, reg2_value, reg3_value, rm);

    latch_fp_status(fcsr_status);

    return alu_result;
}
}//namespace Atlas
