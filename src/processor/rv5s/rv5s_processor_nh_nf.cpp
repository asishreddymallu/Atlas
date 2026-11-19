/**
 * @file rv5s_processor_nh_nf.cpp
 * @brief Implementation for the 5-stage pipelined VM (RV5S) in Mode 1: No Hazard Detection, No
 * Forwarding (NH_NF).
 * * Data Hazards (R-R, L-R, R-Store, L-Store) require 2 NOPs (Programmer Responsibility).
 * * Control Hazards (JAL/JALR, B-Type) are AUTOMATICALLY handled by the pipeline.
 * * Control Penalties: JAL/JALR = 1 bubble (EX stage); B-Type = 2 bubbles (MEM stage, 3-cycle
 * penalty).
 * * @author Atharva and Harshit
 */
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "common/instructions.h"
#include "config/config.h"
#include "common/debug_colors.h"
#include "ui/processor_tab/processor_designs/rv5s_processor_nh_nf_circuit_scene.h"
#include "processor/alu.h"
#include "processor/processor_base.h" // For ImmGenerator, etc.


#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <tuple>

namespace Kites
{
// --- VmBase Pure Virtual Method Implementations (Run, DebugRun, Reset, Step) ---
RV5StageProcessorNHNF::RV5StageProcessorNHNF() : RV5StageVM_Base()
{

// Reset components and history
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Kites::RV5StageVM_NH_NF_CircuitScene>();
    connect(this, &ProcessorBase::circuitSnapshotSignal, circuit_scene_.get(),
            &Kites::RV5StageVM_NH_NF_CircuitScene::updateCircuitSnapshot);
#endif
    Reset();
    active_wires_.append("PC_to_IM");
    active_wires_.append("PCMux_to_PC");
    active_wires_.append("IM_to_P1");
    active_wires_.append("P1_to_P2_PCcarry");
    active_wires_.append("P1_to_Control_RF_andall");
    active_wires_.append("RF_to_P2_UP");
    active_wires_.append("RFdown_to_P2");
    active_wires_.append("P2_to_ALUSRC");
    active_wires_.append("P2_to_ALUSCRMux");
    active_wires_.append("P2_to_ALUControl");
    active_wires_.append("ALUcontrol_to_ALU");
    active_wires_.append("ALUMux_to_ALU");

    active_wires_.append("P2_to_P3_MEMControl");
    active_wires_.append("P2_to_P3_WBcontrol");
    active_wires_.append("P2_to_ALUcontrol_Control");
    // active_wires_.append("P2_to_ALUMux");

    active_wires_.append("Control_to_P3_up");
    active_wires_.append("Control_to_P3_mid");
    active_wires_.append("Control_to_P3_down");

    active_wires_.append("P2_to_P3_WBcontrol");
    active_wires_.append("P2_to_P3_MEMControl");
    active_wires_.append("P2_to_ALUcontrol_Control");

    active_wires_.append("P2_to_ALU2");

    active_wires_.append("P3_to_P4_WBcontrol");

    active_wires_.append("rd_P2_to_P3");
    active_wires_.append("rd_P3_to_P4");

    always_active_wires_count_ = active_wires_.size();
}

void RV5StageProcessorNHNF::SetActiveWireNames()
{
    // Clear and populate canonical wires for the NH_NF circuit (no forwarding)
    resetWiresToBackbone();

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
        lightWire("P2_to_ALUMux", WireStage::EX);
    }

    // EX: a branch's comparison result and the target adder (which a jump
    // uses too), or the ALU result for everything else
    const bool ex_branch = isBranchInstruction(id_ex_reg_.instruction);
    const bool ex_jump   = isJumpInstruction(id_ex_reg_.instruction);
    if (ex_branch)
    {
        lightWire("ALU_zerores_to_P3", WireStage::EX);
    }
    if (ex_branch || ex_jump)
    {
        lightWire("ALU2_to_P3", WireStage::EX);
    }
    else
    {
        lightWire("ALU_to_P3", WireStage::EX);
    }

    // MEM: a branch's target and outcome travel towards the PC mux; any
    // other instruction's ALU result is the data-memory address
    if (isBranchInstruction(ex_mem_reg_.instruction))
    {
        lightWire("ANDGate_lower_entry", WireStage::MEM);
        lightWire("P3_to_PCMux", WireStage::MEM);
    }
    else
    {
        lightWire("P3ALUres_to_DMup", WireStage::MEM);
    }

    // PC selection. A taken branch in MEM selects its target through the
    // AND gate; a jump redirects from EX along a path this drawing does not
    // have, so in that cycle neither input of the PC mux lights.
    if (branchRedirectsNextStep())
    {
        lightWire("ANDGATE_to_PCMUX", WireStage::MEM);
        lightWire("P3_to_UpperEntryANDGATE", WireStage::MEM);
    }
    else if (!jumpRedirectsNextStep())
    {
        lightWire("ALU1_to_PCMuxUp", WireStage::IF);
    }

    // MEM: memory control / data path
    if (ex_mem_reg_.mem_read)
    {
        lightWire("DM_to_P4", WireStage::MEM);
        lightWire("P3_to_DM_Memread", WireStage::MEM);
    }
    if (ex_mem_reg_.mem_write)
    {
        lightWire("P3_TO_DM_control_memwrite", WireStage::MEM);
        lightWire("P3_rs2_to_DMdown", WireStage::MEM);
    }

    // WB: register writeback paths
    if (mem_wb_reg_.reg_write)
    {
        lightWire("P4_wbcontrol_to_WBmux", WireStage::WB);
        lightWire("P4_to_RF_regwritecontrol", WireStage::WB);
        lightWire("WBMux_to_RF", WireStage::WB);
    }
    if (mem_wb_reg_.mem_to_reg)
    {
        lightWire("P4DM_to_lastMux", WireStage::WB);
    }
    else if (mem_wb_reg_.reg_write)
    {
        lightWire("P4_ALUres_to_Mux", WireStage::WB);
        lightWire("RdP4_to_RF", WireStage::WB);
    }
}

void RV5StageProcessorNHNF::Reset()
{
    RV5StageVM_Base::Reset();
}

void RV5StageProcessorNHNF::Step()
{
    begin_step_delta();

    pipeline_writeback();
    pipeline_memory();
    pipeline_execute();
    pipeline_decode();
    fetch_and_advance_pc(false);
    cycle_s_++;

    finalize_step_delta();
}

void RV5StageProcessorNHNF::pipeline_fetch()
{
    if (program_counter_ < program_size_)
    {
        // Latch the instruction and PC for the next stage (IF/ID register)
        if_id_reg_.instruction = memory_controller_.readInstruction(program_counter_);
        if_id_reg_.pc = program_counter_;
    }
    else
    {
        // Once past the end of the program, inject NOPs to drain the pipeline.
        if_id_reg_.reset();
    }
}

void RV5StageProcessorNHNF::pipeline_execute()
{
    uint32_t instruction = id_ex_reg_.instruction;
    uint8_t opcode = instruction & 0b1111111;
    uint8_t funct3 = (instruction >> 12) & 0x7;

    // An ecall/ebreak (known_issues 66) and a CSR instruction (104) are not
    // served here but in writeback, once everything older has retired
    // (serve_syscall_at_writeback, serve_csr_at_writeback). Both flow down
    // the pipe like on the other three variants. A CSR instruction used to
    // return here before EX/MEM was latched, so it never retired and the
    // instruction before it went through MEM and WB twice.

    // Select ALU inputs
    uint64_t alu_in1 = id_ex_reg_.reg1_data;
    uint64_t alu_in2 =
        id_ex_reg_.alu_src ? static_cast<uint64_t>(id_ex_reg_.imm) : id_ex_reg_.reg2_data;

    // Get the specific ALU operation
    alu::AluOp alu_operation =
        control_unit_.GetAluSignalFor(id_ex_reg_.instruction, id_ex_reg_.alu_op);

    // Execute the operation
    bool overflow; // Ignored for this simple model
    uint64_t alu_result = 0;
    const bool is_f_instruction = instruction_set::isFInstruction(instruction);
    const bool is_d_instruction = instruction_set::isDInstruction(instruction);
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
        std::tie(alu_result, overflow) = alu::Alu::execute(alu_operation, alu_in1, alu_in2);
    }
    if ((id_ex_reg_.instruction & 0b1111111) == 0b0110111) // lui
    {
        alu_result = static_cast<uint64_t>(id_ex_reg_.imm << 12);
    }
    if ((id_ex_reg_.instruction & 0b1111111) == 0b0010111) // auipc (known_issues 103)
    {
        alu_result = auipc_result(id_ex_reg_);
    }

    // Latch data for EX/MEM Register
    ex_mem_reg_.prev_reg_write = ex_mem_reg_.reg_write;
    ex_mem_reg_.prev_rd = ex_mem_reg_.rd;
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
    ex_mem_reg_.reg2_data = id_ex_reg_.reg2_data;
    ex_mem_reg_.freg2_data = id_ex_reg_.freg2_data;
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

    // --- Conditional Branch Check (B-type: BLT, BGE, etc.) ---
    if (id_ex_reg_.branch && opcode == 0b1100011)
    {
        bool condition_met = false;

        // This fully implements all six branch conditions using the ALU subtraction/comparison
        // result.
        switch (funct3)
        {
        case 0b000:
            condition_met = (alu_result == 0);
            break; // BEQ (Result of Subtraction is Zero)
        case 0b001:
            condition_met = (alu_result != 0);
            break; // BNE (Result of Subtraction is Non-Zero)
        case 0b100:
            condition_met = (alu_result == 1);
            break; // BLT (Result of kSlt is 1)
        case 0b101:
            condition_met = (alu_result == 0);
            break; // BGE (Result of kSlt is 0 - Not Less Than)
        case 0b110:
            condition_met = (alu_result == 1);
            break; // BLTU (Result of kSltu is 1)
        case 0b111:
            condition_met = (alu_result == 0);
            break; // BGEU (Result of kSltu is 0 - Not Less Than Unsigned)
        }

        if (condition_met)
        {
            // Misprediction detected. Flag MEM stage to handle the 3-cycle flush.
            ex_mem_reg_.branch_taken = true;
            ex_mem_reg_.branch_target_pc = id_ex_reg_.pc + id_ex_reg_.imm;
        }
    }
    // --- Unconditional Jump Check (JAL/JALR: 1-cycle penalty) ---
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

        // 1. Redirect PC (Auto-Advance)
        redirect_pc(jump_target);

        // 2. Kill the next instruction (IF/ID register) to incur the 1-bubble penalty.
        if_id_reg_.reset();

        // 3. Mark as taken (for link register write)
        ex_mem_reg_.branch_taken = true;
    }
}

// --- Undo/Redo Implementations ---


// --- FP Execute Handlers ---

uint64_t RV5StageProcessorNHNF::pipeline_execute_float()
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
        reg1_value = registers_.ReadGpr(id_ex_reg_.rs1);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    alu::AluOp aluOperation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    latch_fp_status(fcsr_status);
    return alu_result;
}

uint64_t RV5StageProcessorNHNF::pipeline_execute_double()
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
        reg1_value = registers_.ReadGpr(id_ex_reg_.rs1);
    }

    if (id_ex_reg_.alu_src)
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(id_ex_reg_.imm));
    }

    alu::AluOp alu_operation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
    std::tie(alu_result, fcsr_status) =
        alu::Alu::dfpexecute(alu_operation, reg1_value, reg2_value, reg3_value, rm);

    latch_fp_status(fcsr_status);
    return alu_result;
}
}//namespace Kites
