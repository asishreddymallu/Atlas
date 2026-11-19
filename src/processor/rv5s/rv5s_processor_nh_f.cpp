/**
 * @file rv5s_processor_nh_f.cpp
 * @brief Implementation for the 5-stage pipelined VM (RV5S) with Forwarding.
 * * * This mode implements all four necessary forwarding paths.
 * * Data Hazards: Solved by forwarding, EXCEPT for Load-Use/Load-Branch.
 * * Programmer Responsibility: Manually insert ONE NOP after Load→Use or Load→Branch.
 * * Control Hazards: AUTOMATICALLY handled by the pipeline flush mechanism.
 * * @author Atharva and Harshit
 */
#include "processor/rv5s/rv5s_forwarding.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "common/instructions.h"
#include "config/config.h"
#include "ui/processor_tab/processor_designs/rv5s_processor_nh_f_circuit_scene.h"
#include "processor/alu.h"
#include "processor/processor_base.h" // For ImmGenerator, etc.
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <tuple>

namespace Kites
{
using namespace alu;

// --- VmBase Pure Virtual Method Implementations (Run, DebugRun, Reset, Step) ---
RV5StageProcessorNHF::RV5StageProcessorNHF() : RV5StageVM_Base()
{
// Reset components and history
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Kites::RV5StageVM_NH_F_CircuitScene>();
    connect(this, &ProcessorBase::circuitSnapshotSignal, circuit_scene_.get(),
            &Kites::RV5StageVM_NH_F_CircuitScene::updateCircuitSnapshot);
#endif
    Reset();

    // Always-visible / backbone wires in the NH_F circuit
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

    // Pipeline/ALU control and data path wires
    active_wires_.append("P2_to_FMUX");
    active_wires_.append("P2_to_FMUX2_UP");
    active_wires_.append("P2_to_ALUMux");
    active_wires_.append("P2_to_ALUcontrol_Control");

    always_active_wires_count_ = active_wires_.size();
}

void RV5StageProcessorNHF::SetActiveWireNames()
{
    // Clear current dynamic list and populate canonical always-active wires first
    resetWiresToBackbone();

    // Every condition below reads the register holding the instruction its
    // stage label shows after this Step (setProcessorState): ID = if_id_reg_,
    // EX = id_ex_reg_, MEM = ex_mem_reg_, WB = mem_wb_reg_. Never a prev_*
    // shadow - those are one generation old here (known_issues 95).

    // ID: control unit and immediate generator into ID/EX
    const RV5SControlUnit id = idStageControl();
    if (id.GetAluSrc())
    {
        lightWire("Imm_to_P2", WireStage::ID);
    }
    if (id.GetMemWrite())
    {
        lightWire("Control_to_P2_MEM", WireStage::ID);
    }
    if (id.GetRegWrite())
    {
        lightWire("Control_to_P2_WB", WireStage::ID);
    }

    // EX: immediate selected as the ALU's second operand
    if (id_ex_reg_.alu_src)
    {
        lightWire("P2Imm_to_ALU2_down", WireStage::EX);
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

    // Forwarding unit activity: drive FWD unit wires when forwarding is relevant
    // EX/MEM forwarding (highest priority)
    if (producerWritesGpr(ex_mem_reg_.instruction, ex_mem_reg_.reg_write) &&
        ex_mem_reg_.rd != 0)
    {
        // producerWritesGpr, not reg_write: the control unit sets reg_write
        // for every OP-FP instruction and the EX/MEM rd field carries the
        // same five bits as frd, so the bare test lit the integer
        // forwarding path for `fadd.d f5, ...` followed by anything that
        // reads x5 - a path with nothing travelling along it. The same
        // substitution 79 made in the datapath (known_issues 81).
        // If EX/MEM destination matches ID/EX sources, show forwarding path
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

    // MEM/WB forwarding (lower priority), with the same guard. mem_wb_reg_,
    // not its prev_* copy: pipeline_memory() copies exactly these fields
    // into prev_* at the start of the next Step, which is what the EX of the
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
}

void RV5StageProcessorNHF::Reset()
{
    RV5StageVM_Base::Reset();
}

void RV5StageProcessorNHF::Step()
{
    begin_step_delta();

    // 1. Execute stages (WB -> MEM -> EX -> ID -> IF)
    pipeline_writeback();
    pipeline_memory();
    pipeline_execute();
    pipeline_decode();
    // 2. Fetch and PC update
    fetch_and_advance_pc(false);
    cycle_s_++; // One clock cycle has passed

    finalize_step_delta();
}

// --- Pipeline Stage Implementations (Full Proof Control + Forwarding) ---

void RV5StageProcessorNHF::pipeline_fetch()
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

void RV5StageProcessorNHF::pipeline_execute()
{

    uint32_t instruction = id_ex_reg_.instruction;
    alu::AluOp alu_operation = control_unit_.GetAluSignalFor(instruction, id_ex_reg_.alu_op);
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
        // ALU execution with forwarding

        // Initial ALU inputs are the stale values read from the register file (ID_EX)
        uint64_t alu_in1 = id_ex_reg_.reg1_data;
        uint64_t alu_in2 = id_ex_reg_.reg2_data;

        // If AluSrc is true (I-type ALU, Load, Store), override reg2_data with the immediate
        if (id_ex_reg_.alu_src)
        {
            alu_in2 = static_cast<uint64_t>(id_ex_reg_.imm);
        }

        // Forwarding values (Source = 00 means no forwarding, use ID_EX value)
        uint8_t forward_a = 0; // 2 = EX/MEM, 1 = MEM/WB
        uint8_t forward_b = 0;

        // --- FORWARDING LOGIC (All non-Load-Use/Branch cases solved here) ---

        // **Priority 1: EX/MEM (Data available from current instruction in MEM)**
        // Covers R->R, R->Branch, R->MemAddr, R->Store (Cases 1, 5, 7, 3)
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
            {
                forward_a = 2; // Forward A from EX/MEM ALU result
            }
            if (ex_mem_reg_.rd == id_ex_reg_.rs2)
            {
                forward_b = 2; // Forward B from EX/MEM ALU result
            }
        }

        // **Priority 2: MEM/WB (Data available from current instruction in WB)**
        // Used for dependencies two cycles ago, and to forward Load data after the 1 NOP delay.
        if (producerWritesGpr(mem_wb_reg_.prev_instruction, mem_wb_reg_.prev_reg_write) &&
            (mem_wb_reg_.prev_rd != 0))
        {
            // Forward A from MEM/WB unless EX/MEM is forwarding to the same register (Priority)
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs1 && forward_a != 2)
            {
                forward_a = 1; // Forward A from MEM/WB result (ALU or Memory)
            }
            // Forward B from MEM/WB unless EX/MEM is forwarding to the same register (Priority)
            if (mem_wb_reg_.prev_rd == id_ex_reg_.rs2 && forward_b != 2)
            {
                forward_b = 1; // Forward B from MEM/WB result (ALU or Memory)
            }
        }

        // Forwarding Source A
        if (forward_a == 2)
        { // Forward from EX/MEM
            alu_in1 = ex_mem_reg_.alu_result;
        }
        else if (forward_a == 1)
        { // Forward from MEM/WB
            alu_in1 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        // Forwarding Source B
        if (forward_b == 2)
        { // Forward from EX/MEM
            alu_in2 = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        { // Forward from MEM/WB
            alu_in2 = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                  : mem_wb_reg_.prev_alu_result;
        }

        // Re-apply immediate value check after forwarding for ALU_B if needed
        if (id_ex_reg_.alu_src && id_ex_reg_.alu_op != 0)
        {
            alu_in2 = static_cast<uint64_t>(id_ex_reg_.imm);
        }
        else if (id_ex_reg_.alu_src && (id_ex_reg_.mem_read || id_ex_reg_.mem_write))
        {
            alu_in2 = id_ex_reg_.alu_src ? static_cast<uint64_t>(id_ex_reg_.imm) : alu_in2;
        }

        // --- EXECUTION ---
        std::tie(alu_result, overflow) = alu::Alu::execute(alu_operation, alu_in1, alu_in2);

        if ((id_ex_reg_.instruction & 0b1111111) == 0b0110111) // lui
        {
            alu_result = static_cast<uint64_t>(id_ex_reg_.imm << 12);
        }
        if ((id_ex_reg_.instruction & 0b1111111) == 0b0010111) // auipc (known_issues 103)
        {
            alu_result = auipc_result(id_ex_reg_);
        }

        // Latch data for EX/MEM Register

        // CRITICAL: The data to be stored (reg2_data for Store) must ALSO be forwarded!
        uint64_t store_data = id_ex_reg_.reg2_data;
        if (forward_b == 2)
        { // Forward from EX/MEM
            store_data = ex_mem_reg_.alu_result;
        }
        else if (forward_b == 1)
        { // Forward from MEM/WB
            store_data = mem_wb_reg_.prev_mem_to_reg ? mem_wb_reg_.prev_memory_data
                                                     : mem_wb_reg_.prev_alu_result;
        }
        ex_mem_reg_.reg2_data = store_data;
    }

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

    // --- Conditional Branch Check (B-type) ---
    if (id_ex_reg_.branch && opcode == 0b1100011)
    {
        bool condition_met = false;
        uint8_t funct3 = (instruction >> 12) & 0x7;

        // Comparison check based on ALU result
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

        redirect_pc(jump_target);
        if_id_reg_.reset();
        ex_mem_reg_.branch_taken = true;
    }
}

uint64_t RV5StageProcessorNHF::pipeline_execute_float()
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
    // otherwise forward its value into x[rs1] (known_issues 76).
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

uint64_t RV5StageProcessorNHF::pipeline_execute_double()
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
        // two casts are necessary here the inner one extends the sign
        //  and then we cast it back to uint64_t
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

}//namespace Kites
