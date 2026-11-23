/**
 * @file rvss_vm.h
 * @brief RVSS VM definition
 * @author Vishank Singh, https://github.com/VishankSingh
 */
#ifndef RVSS_VM_H
#define RVSS_VM_H

#include "processor/processor_base.h"
#include "processor/processor_manager.h"
#include "rvss_control_unit.h"

#include <cstdint>
#include <iostream>
#include <stack>
#include <vector>


// TODO: use a circular buffer instead of a stack for undo/redo
namespace Atlas
{
struct RVSingleStageStepDelta : public StepDelta
{
    // uint64_t old_pc;
    // uint64_t new_pc;
    // std::vector<RegisterChange> register_changes;
    // std::vector<MemoryChange> memory_changes;
};

class RVSSProcessor : public ProcessorBase
{
  public:
    RVSSControlUnit control_unit_;
    // intermediate variables
    int64_t execution_result_{};
    int64_t memory_result_{};
    // int64_t memory_address_{};
    // int64_t memory_data_{};
    uint64_t return_address_{};

    bool branch_flag_ = false;
    int64_t next_pc_{}; // for jal, jalr,

    // CSR intermediate variables
    uint16_t csr_target_address_{};
    uint64_t csr_old_value_{};
    uint64_t csr_write_val_{};
    uint8_t csr_uimm_{};

    std::stack<StepDelta> undo_stack_;
    std::stack<StepDelta> redo_stack_;

    RVSingleStageStepDelta current_delta_;

    void Fetch();

    void Decode();

    void Execute();
    void ExecuteFloat();
    void ExecuteDouble();
    void ExecuteCsr();
    /// Writes an F/D op's flags to fflags and records the change for Undo.
    void WriteFpStatus(uint8_t status);
    void HandleSyscall();

    void WriteMemory();
    void WriteMemoryFloat();
    void WriteMemoryDouble();

    void WriteBack();
    void WriteBackFloat();
    void WriteBackDouble();
    void WriteBackCsr();

    RVSSProcessor();
    ~RVSSProcessor();

    void Run() override;
    void DebugRun() override;
    void Step() override;
    bool StepAndNotify() override;
    void Undo() override;
    void Redo() override;
    void Reset() override;

    void SetActiveWireNames() override;
    /// The one CI cell: the instruction just executed - the one the wires
    /// describe (current_instruction_ at last_executed_pc_). Undo does not
    /// restore current_instruction_; see known_issues 97.
    QList<StageCell> stageCells() const override;
    void setProcessorState() override;

    void PrintType()
    {
        std::cout << "rvssvm" << std::endl;
    }
};
}//namespace Atlas
#endif // RVSS_VM_H
