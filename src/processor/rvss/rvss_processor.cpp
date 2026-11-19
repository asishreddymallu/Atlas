/**
 * @file rvss_vm.cpp
 * @brief RVSS VM implementation
 * @author Vishank Singh, https://github.com/VishankSingh
 */

#include "processor/rvss/rvss_processor.h"

#include "common/instructions.h"
#include "config/config.h"
#include "common/globals.h"
#include "ui/processor_tab/processor_designs/rvss_processor_circuit_scene.h"
#include "utils/utils.h"
#include <QDebug>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <queue>
#include <stack>
#include <thread>
#include <tuple>

namespace Kites
{
RVSSProcessor::RVSSProcessor() : ProcessorBase()
{
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
#ifndef DISABLE_GUI
    circuit_scene_ = std::make_unique<Kites::RVSSCircuitScene>();
    connect(this, &ProcessorBase::circuitSnapshotSignal, circuit_scene_.get(),
            &Kites::RVSSCircuitScene::updateCircuitSnapshot, Qt::QueuedConnection);
#endif

    // IM_to_PC_pc and PC_to_IM_instruction used to head this list.
    // Neither is a wire: single_cycle_draft1.json holds 22 wires and all
    // 22 are named, so there was nothing for either to refer to. The two
    // wires they plausibly meant are already here - the instruction
    // leaving instruction memory is IM_to_RF_rs1_whole immediately
    // below, and the address reaching it is PC_to_InstrMem further down.
    // They read as leftovers from the pipelined drawings' naming, where
    // the same two wires are PC_to_IM and IM_to_P1.
    active_wires_.append("IM_to_RF_rs1_whole");
    // ...Mux, not ...MUX. Imm_to_ALUMUX below really is spelled with the
    // capitals in the drawing; this one is not, so the register file's
    // output into the ALU mux was never highlighted at all.
    active_wires_.append("RF_to_ALUMux");
    active_wires_.append("PCMux_to_PC");
    active_wires_.append("Imm_to_ALUMUX");
    active_wires_.append("BranchALU_to_PCMUX");
    active_wires_.append("PCALU_to_PCMUX");
    active_wires_.append("Control_ALUOp");
    active_wires_.append("PC_to_InstrMem");
    active_wires_.append("ALUOp_to_ALU_alusignal");
    active_wires_.append("ALU_to_DM");

    always_active_wires_count_ = active_wires_.size();
}

RVSSProcessor::~RVSSProcessor() = default;

QList<StageCell> RVSSProcessor::stageCells() const
{
    if (last_executed_pc_ == INVALID_PC)
        return {StageCell{}};
    return {StageCell{current_instruction_, last_executed_pc_}};
}

void RVSSProcessor::SetActiveWireNames()
{
    // Start from the constructor's always-active backbone, as the four
    // pipelined models do. Without this, every caller that does not erase
    // afterwards - DebugRun, Undo, Redo - leaves the previous cycle's
    // wires lit and grows the list on every press (known_issues 96).
    resetWiresToBackbone();

    // Map control signals to canonical wire names used in the scene JSON.
    // maybe sending integers would be better?
    // but for now, this is fine.
    // also the preformace boost well get of that will that really improve the user experience that
    // much?

    // first add the always active wires
    // this is not ideal but for now it will do

    // if(branch_flag_)
    // {

    // }

    if (control_unit_.GetMemWrite())
    {
        active_wires_.append("Control_to_MemWrite");
    }
    if (control_unit_.GetMemRead())
    {
        active_wires_.append("Control_to_MemRead");
    }
    if (control_unit_.GetRegWrite())
    {
        active_wires_.append("Control_to_Regwrite");
        active_wires_.append("WBMux_to_RF_lowest");
    }
    if (control_unit_.GetBranch())
    {
        active_wires_.append("Control_to_BranchAND");
    }
    if (control_unit_.GetAluSrc())
    {
        active_wires_.append("Control_to_ALUSrc_control");
    }
    if (control_unit_.GetMemToReg())
    {
        active_wires_.append("Control_to_MemtoRegMux");
    }

}

void RVSSProcessor::setProcessorState()
{
    processor_state_.programCounters = 
    {program_counter_,INVALID_PC,INVALID_PC,INVALID_PC,INVALID_PC};
    processor_state_.lastExecutedPC = last_executed_pc_;
    processor_state_.instructionsRetired = instructions_retired_;
    // Never set on rvss before, so CPI and IPC read 0. One instruction a
    // cycle: both are 1 once anything has run.
    cpi_ = instructions_retired_ ? static_cast<double>(cycle_s_) / instructions_retired_ : 0.0;
    ipc_ = cycle_s_ ? static_cast<double>(instructions_retired_) / cycle_s_ : 0.0;
}

void RVSSProcessor::Run()
{
    RunScope scope(*this); // marks the worker busy; see the header's invariant
    while (!stop_requested_ && program_counter_ < program_size_)
    {
        if (last_breakpoint_pc_ != UINT64_MAX && program_counter_ != last_breakpoint_pc_)
        {
            last_breakpoint_pc_ = UINT64_MAX;
        }

        if (std::find(breakpoints_.begin(), breakpoints_.end(), program_counter_) !=
                breakpoints_.end() &&
            program_counter_ != last_breakpoint_pc_)
        {
            pause_requested_ = true;
            last_breakpoint_pc_ = program_counter_;
            emit processorPausedAtBreakpointSignal();
        }
        {
            QMutexLocker locker(&pause_mutex_);
            while (pause_requested_ && !stop_requested_)
            {
                pause_wait_condition_.wait(&pause_mutex_);
            }
            if (stop_requested_)
            {
                break;
            }
        }
        StepAndNotify();
        // handling the delay
        {
            QMutexLocker locker(&pause_mutex_);
            if (stop_requested_ || pause_requested_)
                continue;
            pause_wait_condition_.wait(&pause_mutex_, step_delay_);
            // we wait for step_delay_ milliseconds or until notified to wake up
        }
    }
    if (stop_requested_)
    {
        emit processorClockedSignal(processor_state_);
    }
    if (program_counter_ >= program_size_)
    {
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

void RVSSProcessor::Fetch()
{
    current_instruction_ = memory_controller_.readInstruction(program_counter_);
    UpdateProgramCounter(4);
}

void RVSSProcessor::Decode()
{
    control_unit_.SetControlSignals(current_instruction_);
}

void RVSSProcessor::Execute()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        HandleSyscall();
        return;
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        ExecuteFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        ExecuteDouble();
        return;
    }
    else if (opcode == 0b1110011)
    {
        ExecuteCsr();
        return;
    }

    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    int32_t imm = ImmGenerator(current_instruction_);

    uint64_t reg1_value = registers_.ReadGpr(rs1);
    uint64_t reg2_value = registers_.ReadGpr(rs2);

    bool overflow = false;

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, overflow) = alu_.execute(aluOperation, reg1_value, reg2_value);

    if (control_unit_.GetBranch())
    {
        if (opcode == 0b1100111 || opcode == 0b1101111)
        {                                                      // JALR or JAL
            next_pc_ = static_cast<int64_t>(program_counter_); // PC was already updated in Fetch()
            UpdateProgramCounter(-4);
            return_address_ = program_counter_ + 4;
            if (opcode == 0b1100111)
            { // JALR
                UpdateProgramCounter(-program_counter_ + (execution_result_));
            }
            else
            { // JAL
                UpdateProgramCounter(imm);
            }
        }
        else if (opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbeq)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbne)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kblt)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbge)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbltu)
                               .opcode ||
                 opcode == instruction_set::instruction_encoding_map
                               .at(instruction_set::Instruction::kbgeu)
                               .opcode)
        {
            switch (funct3)
            {
            case 0b000:
            { // BEQ
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            case 0b001:
            { // BNE
                branch_flag_ = (execution_result_ != 0);
                break;
            }
            case 0b100:
            { // BLT
                branch_flag_ = (execution_result_ == 1);
                break;
            }
            case 0b101:
            { // BGE
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            case 0b110:
            { // BLTU
                branch_flag_ = (execution_result_ == 1);
                break;
            }
            case 0b111:
            { // BGEU
                branch_flag_ = (execution_result_ == 0);
                break;
            }
            }
        }
    }

    if (branch_flag_ &&
        (opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbeq)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbne)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kblt)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbge)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbltu)
                       .opcode ||
         opcode == instruction_set::instruction_encoding_map.at(instruction_set::Instruction::kbgeu)
                       .opcode))
    {
        UpdateProgramCounter(-4);
        UpdateProgramCounter(imm);
        // we have jumped so the branch alu wire will send signal to the pc mux
        active_wires_.append("ALUzero_to_ANDGATElower");
        active_wires_.append("BranchAND_to_PCMUX");
    }

    if (opcode == 0b0010111)
    { // AUIPC
        execution_result_ = static_cast<int64_t>(program_counter_) - 4 + (imm << 12);
    }
}

void RVSSProcessor::ExecuteFloat()
{
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t rm = funct3;
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t rs3 = (current_instruction_ >> 27) & 0b11111;

    uint8_t fcsr_status = 0;

    int32_t imm = ImmGenerator(current_instruction_);

    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = registers_.ReadFpr(rs1);
    uint64_t reg2_value = registers_.ReadFpr(rs2);
    uint64_t reg3_value = registers_.ReadFpr(rs3);

    if (instruction_set::readsIntegerSource(current_instruction_))
    {
        reg1_value = registers_.ReadGpr(rs1);
    }

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, fcsr_status) =
        alu::Alu::fpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    // The flags go to fflags = fcsr[4:0]; writing all of fcsr cleared frm
    // (known_issues 104). Replace semantics and encoding unchanged (72).
    WriteFpStatus(fcsr_status);
}

void RVSSProcessor::ExecuteDouble()
{
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t rm = funct3;
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t rs3 = (current_instruction_ >> 27) & 0b11111;

    uint8_t fcsr_status = 0;

    int32_t imm = ImmGenerator(current_instruction_);

    // A dynamic rm (0b111, the assembler's default) means "use frm", as in
    // ExecuteFloat and on every pipeline. Passing 7 through left the ALU in
    // whatever mode the thread was already in (known_issues 107).
    if (rm == 0b111)
    {
        rm = registers_.ReadCsr(0x002);
    }

    uint64_t reg1_value = registers_.ReadFpr(rs1);
    uint64_t reg2_value = registers_.ReadFpr(rs2);
    uint64_t reg3_value = registers_.ReadFpr(rs3);

    if (instruction_set::readsIntegerSource(current_instruction_))
    {
        reg1_value = registers_.ReadGpr(rs1);
    }

    if (control_unit_.GetAluSrc())
    {
        reg2_value = static_cast<uint64_t>(static_cast<int64_t>(imm));
    }

    alu::AluOp aluOperation =
        control_unit_.GetAluSignal(current_instruction_, control_unit_.GetAluOp());
    std::tie(execution_result_, fcsr_status) =
        alu::Alu::dfpexecute(aluOperation, reg1_value, reg2_value, reg3_value, rm);

    // The status was computed and dropped, while the pipelines wrote it
    // (known_issues 108). Same write as ExecuteFloat: fflags, recorded for
    // Undo.
    WriteFpStatus(fcsr_status);
}

void RVSSProcessor::WriteFpStatus(uint8_t status)
{
    // Recorded in the step delta, so Undo restores the flags; it was not,
    // and Undo left the last FP op's flags behind (known_issues 115).
    // The flags accrue (OR), as RISC-V's fflags do; a status of 0 - a load,
    // a store, an exact op - changes nothing (known_issues 72, 116).
    const uint64_t old_fcsr = registers_.ReadCsr(0x003);
    registers_.WriteCsr(0x001, registers_.ReadCsr(0x001) | status);
    const uint64_t new_fcsr = registers_.ReadCsr(0x003);
    if (old_fcsr != new_fcsr)
        current_delta_.register_changes.push_back({0x003, 1, old_fcsr, new_fcsr});
}

void RVSSProcessor::ExecuteCsr()
{
    uint8_t rs1 = (current_instruction_ >> 15) & 0b11111;
    uint16_t csr = (current_instruction_ >> 20) & 0xFFF;
    uint64_t csr_val = registers_.ReadCsr(csr);

    csr_target_address_ = csr;
    csr_old_value_ = csr_val;
    csr_write_val_ = registers_.ReadGpr(rs1);
    csr_uimm_ = rs1;
}

void RVSSProcessor::HandleSyscall()
{
    ServeSyscall(current_delta_);
}

void RVSSProcessor::WriteMemory()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        return;
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        WriteMemoryFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        WriteMemoryDouble();
        return;
    }

    if (control_unit_.GetMemRead())
    {
        switch (funct3)
        {
        case 0b000:
        { // LB
            memory_result_ = static_cast<int8_t>(memory_controller_.readByte(execution_result_));
            break;
        }
        case 0b001:
        { // LH
            memory_result_ =
                static_cast<int16_t>(memory_controller_.readHalfWord(execution_result_));
            break;
        }
        case 0b010:
        { // LW
            memory_result_ = static_cast<int32_t>(memory_controller_.readWord(execution_result_));
            break;
        }
        case 0b011:
        { // LD
            memory_result_ = memory_controller_.readDoubleWord(execution_result_);
            break;
        }
        case 0b100:
        { // LBU
            memory_result_ = static_cast<uint8_t>(memory_controller_.readByte(execution_result_));
            break;
        }
        case 0b101:
        { // LHU
            memory_result_ =
                static_cast<uint16_t>(memory_controller_.readHalfWord(execution_result_));
            break;
        }
        case 0b110:
        { // LWU
            memory_result_ = static_cast<uint32_t>(memory_controller_.readWord(execution_result_));
            break;
        }
        }
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    // Undo bookkeeping reads use peekByte so they do not show up as cache accesses.

    if (control_unit_.GetMemWrite())
    {
        switch (funct3)
        {
        case 0b000:
        { // SB
            addr = execution_result_;
            old_bytes_vec.push_back(memory_controller_.peekByte(addr));
            memory_controller_.writeByte(execution_result_, registers_.ReadGpr(rs2) & 0xFF);
            new_bytes_vec.push_back(memory_controller_.peekByte(addr));
            break;
        }
        case 0b001:
        { // SH
            addr = execution_result_;
            for (size_t i = 0; i < 2; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeHalfWord(execution_result_, registers_.ReadGpr(rs2) & 0xFFFF);
            for (size_t i = 0; i < 2; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        case 0b010:
        { // SW
            addr = execution_result_;
            for (size_t i = 0; i < 4; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeWord(execution_result_, registers_.ReadGpr(rs2) & 0xFFFFFFFF);
            for (size_t i = 0; i < 4; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        case 0b011:
        { // SD
            addr = execution_result_;
            for (size_t i = 0; i < 8; ++i)
            {
                old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            memory_controller_.writeDoubleWord(execution_result_,
                                               registers_.ReadGpr(rs2) & 0xFFFFFFFFFFFFFFFF);
            for (size_t i = 0; i < 8; ++i)
            {
                new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
            }
            break;
        }
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteMemoryFloat()
{
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    if (control_unit_.GetMemRead())
    { // FLW
        memory_result_ = memory_controller_.readWord(execution_result_);
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    if (control_unit_.GetMemWrite())
    { // FSW
        addr = execution_result_;
        for (size_t i = 0; i < 4; ++i)
        {
            old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
        uint32_t val = registers_.ReadFpr(rs2) & 0xFFFFFFFF;
        memory_controller_.writeWord(execution_result_, val);
        // new_bytes_vec.push_back(memory_controller_.ReadByte(addr));
        for (size_t i = 0; i < 4; ++i)
        {
            new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteMemoryDouble()
{
    uint8_t rs2 = (current_instruction_ >> 20) & 0b11111;

    if (control_unit_.GetMemRead())
    { // FLD
        memory_result_ = memory_controller_.readDoubleWord(execution_result_);
    }

    uint64_t addr = 0;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;

    if (control_unit_.GetMemWrite())
    { // FSD
        addr = execution_result_;
        for (size_t i = 0; i < 8; ++i)
        {
            old_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
        memory_controller_.writeDoubleWord(execution_result_, registers_.ReadFpr(rs2));
        for (size_t i = 0; i < 8; ++i)
        {
            new_bytes_vec.push_back(memory_controller_.peekByte(addr + i));
        }
    }

    if (old_bytes_vec != new_bytes_vec)
    {
        current_delta_.memory_changes.push_back({addr, old_bytes_vec, new_bytes_vec});
    }
}

void RVSSProcessor::WriteBack()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;
    int32_t imm = ImmGenerator(current_instruction_);

    if (opcode == 0b1110011 && funct3 == 0b000)
    {
        return;
    }

    if (instruction_set::isFInstruction(current_instruction_))
    { // RV64 F
        WriteBackFloat();
        return;
    }
    else if (instruction_set::isDInstruction(current_instruction_))
    {
        WriteBackDouble();
        return;
    }
    else if (opcode == 0b1110011)
    { // CSR opcode
        WriteBackCsr();
        return;
    }

    uint64_t old_reg = registers_.ReadGpr(rd);
    unsigned int reg_index = rd;
    unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR

    if (control_unit_.GetRegWrite())
    {
        switch (opcode)
        {
        case 0b0110011: // R-Type
        case 0b0010011: // I-Type
        case 0b0010111: // AUIPC
        case 0b0111011: // R4-Type / OP-32 (ADDW, SUBW, MULW, DIVW, DIVUW, REMW, REMUW, SLLW, SRLW, SRAW)
        case 0b0011011: // I-type OP-32 (ADDIW, SLLIW, SRLIW, SRAIW)
        {
            registers_.WriteGpr(rd, execution_result_);
            break;
        }
        case 0b0000011:
        { // Load
            registers_.WriteGpr(rd, memory_result_);
            break;
        }
        case 0b1100111: // JALR
        case 0b1101111:
        { // JAL
            registers_.WriteGpr(rd, next_pc_);
            break;
        }
        case 0b0110111:
        { // LUI
            registers_.WriteGpr(rd, (imm << 12));
            break;
        }
        default:
            break;
        }
    }

    if (opcode == 0b1101111)
    { // JAL
      // Updated in Execute()
    }
    if (opcode == 0b1100111)
    { // JALR
      // registers_.WriteGpr(rd, return_address_); // Write back to rs1
      // Updated in Execute()
    }

    uint64_t new_reg = registers_.ReadGpr(rd);
    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

void RVSSProcessor::WriteBackFloat()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    if (control_unit_.GetRegWrite())
    {
        // write to GPR
        // The funct7 test below only means anything for opcode
        // 0b1010011. An R4-type FMA's bits 31:25 are rs3<<2 | fmt, so
        // `fmadd.s fd, f1, f2, f20` reads 0b1010000 and would send the
        // result to the INTEGER register x[rd]; rs3 = 24 and 28 collide
        // the same way, and .d collides with the ...0001 row
        // (known_issues 61).
        if (opcode == 0b1010011 &&
            (funct7 == 0b1010000 || funct7 == 0b1100000 || funct7 == 0b1110000))
        { // f(eq|lt|le).s, fcvt.(w|wu|l|lu).s
            old_reg = registers_.ReadGpr(rd);
            registers_.WriteGpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 0; // GPR
        }
        // write to FPR
        else if (opcode == 0b0000111)
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, memory_result_);
            new_reg = memory_result_;
            reg_type = 2; // FPR
        }
        else
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 2; // FPR
        }
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }
}

void RVSSProcessor::WriteBackDouble()
{
    uint8_t opcode = current_instruction_ & 0b1111111;
    uint8_t funct7 = (current_instruction_ >> 25) & 0b1111111;
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;

    uint64_t old_reg = 0;
    unsigned int reg_index = rd;
    unsigned int reg_type = 2; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t new_reg = 0;

    if (control_unit_.GetRegWrite())
    {
        // write to GPR
        // The funct7 test below only means anything for opcode
        // 0b1010011. An R4-type FMA's bits 31:25 are rs3<<2 | fmt, so
        // `fmadd.s fd, f1, f2, f20` reads 0b1010000 and would send the
        // result to the INTEGER register x[rd]; rs3 = 24 and 28 collide
        // the same way, and .d collides with the ...0001 row
        // (known_issues 61).
        if (opcode == 0b1010011 &&
            (funct7 == 0b1010001 || funct7 == 0b1100001 || funct7 == 0b1110001))
        { // f(eq|lt|le).d, fcvt.(w|wu|l|lu).d
            old_reg = registers_.ReadGpr(rd);
            registers_.WriteGpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 0; // GPR
        }
        // write to FPR
        else if (opcode == 0b0000111)
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, memory_result_);
            new_reg = memory_result_;
            reg_type = 2; // FPR
        }
        else
        {
            old_reg = registers_.ReadFpr(rd);
            registers_.WriteFpr(rd, execution_result_);
            new_reg = execution_result_;
            reg_type = 2; // FPR
        }
    }

    if (old_reg != new_reg)
    {
        current_delta_.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
    }

    return;
}

void RVSSProcessor::WriteBackCsr()
{
    uint8_t rd = (current_instruction_ >> 7) & 0b11111;
    uint8_t funct3 = (current_instruction_ >> 12) & 0b111;

    uint64_t old_gpr = registers_.ReadGpr(rd);
    uint64_t old_csr = registers_.ReadCsr(csr_target_address_);

    switch (funct3)
    {
    case 0b001:
    { // CSRRW
        registers_.WriteGpr(rd, csr_old_value_);
        registers_.WriteCsr(csr_target_address_, csr_write_val_);
        break;
    }
    case 0b010:
    { // CSRRS
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_write_val_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ | csr_write_val_);
        }
        break;
    }
    case 0b011:
    { // CSRRC
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_write_val_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ & ~csr_write_val_);
        }
        break;
    }
    case 0b101:
    { // CSRRWI
        registers_.WriteGpr(rd, csr_old_value_);
        registers_.WriteCsr(csr_target_address_, csr_uimm_);
        break;
    }
    case 0b110:
    { // CSRRSI
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_uimm_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ | csr_uimm_);
        }
        break;
    }
    case 0b111:
    { // CSRRCI
        registers_.WriteGpr(rd, csr_old_value_);
        if (csr_uimm_ != 0)
        {
            registers_.WriteCsr(csr_target_address_, csr_old_value_ & ~csr_uimm_);
        }
        break;
    }
    }

    uint64_t new_gpr = registers_.ReadGpr(rd);
    uint64_t new_csr = registers_.ReadCsr(csr_target_address_);

    if (old_gpr != new_gpr)
    {
        current_delta_.register_changes.push_back({rd, 0, old_gpr, new_gpr});
    }
    if (old_csr != new_csr)
    {
        current_delta_.register_changes.push_back({csr_target_address_, 1, old_csr, new_csr});
    }
}

void RVSSProcessor::DebugRun()
{
    RunScope scope(*this);
    while (!stop_requested_ && program_counter_ < program_size_)
    {
        current_delta_.old_pc = program_counter_;
        if (std::find(breakpoints_.begin(), breakpoints_.end(), program_counter_) ==
            breakpoints_.end())
        {
            Fetch();
            Decode();
            Execute();
            WriteMemory();
            WriteBack();
            instructions_retired_++;
            cycle_s_++;
            std::cout << "Program Counter: " << program_counter_ << std::endl;

            current_delta_.new_pc = program_counter_;
            TraceSingleCycle(current_delta_, current_instruction_);
            // history_.push(current_delta_);
            undo_stack_.push(current_delta_);
            while (!redo_stack_.empty())
            {
                redo_stack_.pop();
            }
            current_delta_ = RVSingleStageStepDelta();
            if (program_counter_ < program_size_)
            {
                std::cout << "VM_STEP_COMPLETED" << std::endl;
                output_status_ = "VM_STEP_COMPLETED";
            }
            else if (program_counter_ >= program_size_)
            {
                std::cout << "VM_LAST_INSTRUCTION_STEPPED" << std::endl;
                output_status_ = "VM_LAST_INSTRUCTION_STEPPED";
            }
            DumpRegisters(globals::registers_dump_file_path, registers_);
            DumpState(globals::vm_state_dump_file_path);
            // update circuit UI after the debug step
            SetActiveWireNames();
            emitCircuitState();

            //unsigned int delay_ms = vm_config::config.getRunStepDelay();
            // std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        else
        {
            std::cout << "VM_BREAKPOINT_HIT " << program_counter_ << std::endl;
            output_status_ = "VM_BREAKPOINT_HIT";
            break;
        }
    }
    if (program_counter_ >= program_size_)
    {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
}

void RVSSProcessor::Step()
{
    current_delta_.old_pc = program_counter_;
    if (program_counter_ < program_size_)
    {
        Fetch();
        Decode();
        Execute();
        WriteMemory();
        WriteBack();
        instructions_retired_++;
        cycle_s_++;
        std::cout << "Program Counter: " << std::hex << program_counter_ << std::dec << std::endl;

        current_delta_.new_pc = program_counter_;

        TraceSingleCycle(current_delta_, current_instruction_);

        // history_.push(current_delta_);

        undo_stack_.push(current_delta_);
        while (!redo_stack_.empty())
        {
            redo_stack_.pop();
        }

        current_delta_ = RVSingleStageStepDelta();

        if (program_counter_ < program_size_)
        {
            std::cout << "VM_STEP_COMPLETED" << std::endl;
            output_status_ = "VM_STEP_COMPLETED";
        }
        else if (program_counter_ >= program_size_)
        {
            std::cout << "VM_LAST_INSTRUCTION_STEPPED" << std::endl;
            output_status_ = "VM_LAST_INSTRUCTION_STEPPED";
        }
    }
    else if (program_counter_ >= program_size_)
    {
        std::cout << "VM_PROGRAM_END" << std::endl;
        output_status_ = "VM_PROGRAM_END";
    }
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);

}

bool RVSSProcessor::StepAndNotify()
{
    QMutexLocker locker(&step_mutex_);
    if (program_counter_ >= program_size_)
    {
        return false;
    }
    last_executed_pc_ = program_counter_;
    Step();
    SetActiveWireNames();
    setProcessorState();
    // Single cycle: what executed is what retired.
    processor_state_.retiredPC = last_executed_pc_;
    emit processorClockedSignal(processor_state_);
    emitCircuitState();
    active_wires_.erase(active_wires_.begin() + always_active_wires_count_,
                        active_wires_.end());
    // clear active wires after emitting signal except always active wires
    // An exit service reached by this manual step ends the program here,
    // whatever the PC says (known_issues 66 / 102).
    const bool exited = TakeProgramExit();
    return !exited && program_counter_ < program_size_;
}

void RVSSProcessor::Undo()
{
    // Mutates undo_stack_/redo_stack_ and the register file, all of which
    // a running worker is writing. Called from the UI thread.
    QMutexLocker locker(&step_mutex_);
    qInfo() << "Attempting to undo last step in rvss";
    if (undo_stack_.empty())
    {
        std::cout << "VM_NO_MORE_UNDO" << std::endl;
        output_status_ = "VM_NO_MORE_UNDO";
        return;
    }

    StepDelta last = undo_stack_.top();
    undo_stack_.pop();

    for (const auto &change : last.register_changes)
    {
        switch (change.reg_type)
        {
        case 0:
        { // GPR
            registers_.WriteGpr(change.reg_index, change.old_value);
            break;
        }
        case 1:
        { // CSR
            registers_.WriteCsr(change.reg_index, change.old_value);
            break;
        }
        case 2:
        { // FPR
            registers_.WriteFpr(change.reg_index, change.old_value);
            break;
        }
        default:
            std::cerr << "Invalid register type: " << change.reg_type << std::endl;
            break;
        }
    }

    for (const auto &change : last.memory_changes)
    {
        for (size_t i = 0; i < change.old_bytes_vec.size(); ++i)
        {
            memory_controller_.writeByte(change.address + i, change.old_bytes_vec[i]);
        }
    }

    program_counter_ = last.old_pc;
    instructions_retired_--;
    cycle_s_--;
    trace_.truncateAfter(cycle_s_);
    trace_next_id_ = last.trace.nextIdBefore;
    std::cout << "Program Counter: " << program_counter_ << std::endl;

    redo_stack_.push(last);

    output_status_ = "VM_UNDO_COMPLETED";
    std::cout << "VM_UNDO_COMPLETED" << std::endl;

    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);

    SetActiveWireNames();
    setProcessorState();
    emitCircuitState();
    emit processorClockedSignal(processor_state_);
    qInfo() << "Undo completed in rvss";
}

void RVSSProcessor::Redo()
{
    QMutexLocker locker(&step_mutex_);
    qInfo() << "Attempting to redo last undone step in rvss";
    if (redo_stack_.empty())
    {
        std::cout << "VM_NO_MORE_REDO" << std::endl;
        return;
    }

    StepDelta next = redo_stack_.top();
    redo_stack_.pop();

    // if (!history_.can_redo()) {
    //       std::cout << "Nothing to redo.\n";
    //       return;
    //   }

    //   StepDelta next = history_.redo();

    for (const auto &change : next.register_changes)
    {
        switch (change.reg_type)
        {
        case 0:
        { // GPR
            registers_.WriteGpr(change.reg_index, change.new_value);
            break;
        }
        case 1:
        { // CSR
            registers_.WriteCsr(change.reg_index, change.new_value);
            break;
        }
        case 2:
        { // FPR
            registers_.WriteFpr(change.reg_index, change.new_value);
            break;
        }
        default:
            std::cerr << "Invalid register type: " << change.reg_type << std::endl;
            break;
        }
    }

    for (const auto &change : next.memory_changes)
    {
        for (size_t i = 0; i < change.new_bytes_vec.size(); ++i)
        {
            memory_controller_.writeByte(change.address + i, change.new_bytes_vec[i]);
        }
    }

    program_counter_ = next.new_pc;
    instructions_retired_++;
    cycle_s_++;
    trace_.truncateAfter(cycle_s_ - 1);
    trace_.push(next.trace);
    trace_next_id_ = next.trace.nextIdAfter;
    DumpRegisters(globals::registers_dump_file_path, registers_);
    DumpState(globals::vm_state_dump_file_path);
    std::cout << "Program Counter: " << program_counter_ << std::endl;
    undo_stack_.push(next);

    SetActiveWireNames();
    setProcessorState();
    emitCircuitState();
    emit processorClockedSignal(processor_state_);
}

void RVSSProcessor::Reset()
{
    program_counter_ = 0;
    instructions_retired_ = 0;
    cycle_s_ = 0;
    exit_cycle_.reset();
    cpi_ = 0;
    ipc_ = 0;
    last_breakpoint_pc_ = UINT64_MAX; // Clear breakpoint tracking on reset
    registers_.Reset();
    memory_controller_.reset();
    control_unit_.Reset();
    branch_flag_ = false;
    next_pc_ = 0;
    execution_result_ = 0;
    memory_result_ = 0;

    return_address_ = 0;
    csr_target_address_ = 0;
    csr_old_value_ = 0;
    csr_write_val_ = 0;
    csr_uimm_ = 0;
    current_delta_.register_changes.clear();
    current_delta_.memory_changes.clear();
    current_delta_.old_pc = 0;
    current_delta_.new_pc = 0;
    undo_stack_ = std::stack<StepDelta>();
    redo_stack_ = std::stack<StepDelta>();
    ResetTrace();
}
}//namespaace Kites
