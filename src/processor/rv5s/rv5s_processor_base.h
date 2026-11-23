#pragma once
#include "processor/pipeline_registers.h"
#include "processor/processor_base.h"
#include "processor/processor_manager.h"
#include "rv5s_control_unit.h"

namespace Atlas
{
struct PipelineRegisterChange
{
    IF_ID_Register old_if_id_reg;
    IF_ID_Register new_if_id_reg;

    ID_EX_Register old_id_ex_reg;
    ID_EX_Register new_id_ex_reg;

    EX_MEM_Register old_ex_mem_reg;
    EX_MEM_Register new_ex_mem_reg;

    MEM_WB_Register old_mem_wb_reg;
    MEM_WB_Register new_mem_wb_reg;
};

struct RV5StageStepDelta : public StepDelta
{
    PipelineRegisterChange pipeline_register_change;
};

class RV5StageVM_Base : public ProcessorBase
{
  public:
    RV5StageVM_Base() = default;
    ~RV5StageVM_Base() = default;

    // --- VM Control Functions ---

    void PrintType()
    {
        std::cout << "rv5s_vm" << std::endl;
    }
    void DebugRun() override;
    void Reset() override;
    void Undo() override;
    void Redo() override;

  protected:
    // Pipeline Registers
    IF_ID_Register if_id_reg_;
    ID_EX_Register id_ex_reg_;
    EX_MEM_Register ex_mem_reg_;
    MEM_WB_Register mem_wb_reg_;

    // The control unit for the pipeline
    RV5SControlUnit control_unit_;

    std::stack<RV5StageStepDelta> undo_stack_{};
    std::stack<RV5StageStepDelta> redo_stack_{};
    RV5StageStepDelta current_delta_;

    // Set when EX/MEM redirected the PC this cycle; consumed by fetch_and_advance_pc().
    bool pc_redirected_ = false;

    void memory_writeback();
    void memory_writeback_float();
    void memory_writeback_double();

    void register_write_back(const uint64_t &write_data);
    void memory_read();
    // void memory_read_float();
    // void memory_read_double();

    void begin_step_delta();
    void finalize_step_delta();

    // --- WP-02 trace (execution_trace.h). Read-only on the machine. ---
    /// Does this variant have a hazard unit / forwarding? Only the trace
    /// asks; the datapath of each variant is unchanged.
    virtual bool TraceHazardUnit() const { return false; }
    virtual bool TraceForwarding() const { return false; }
    /// Called by begin_step_delta: the cycle's occupants, its RAW
    /// dependences, forwarding paths and the redirect it will make, all from
    /// the registers as they stand before the step.
    void trace_begin();
    /// Called by finalize_step_delta: what actually happened (stall,
    /// squashes), the instance ids after the step, then the push.
    void trace_finalize();
    TraceCycle trace_pending_{};

    void print_pipeline_registers_debug();
    bool is_pipeline_drained() const;
    /// Past the last instruction and drained: the pipeline's end.
    /// An ecall is served when it reaches writeback, i.e. at the next step
    /// if it is in MEM/WB now.
    bool NextStepServesEcall() override
    {
        return mem_wb_reg_.pc != INVALID_PC && mem_wb_reg_.instruction == 0x00000073u;
    }
    bool RanOffTheEnd() const override
    {
        return program_size_ > 0 && program_counter_ >= program_size_ && is_pipeline_drained();
    }
    void setProcessorState() override;

    // --- Read-only queries for SetActiveWireNames (known_issues 95) ---
    // SetActiveWireNames runs after Step(), when every pipeline register
    // holds the instruction its stage label shows (setProcessorState):
    // ID = if_id_reg_, EX = id_ex_reg_, MEM = ex_mem_reg_, WB = mem_wb_reg_.
    // That labelled cycle is the one the NEXT Step() executes, so what it
    // does is a function of the registers as they stand. These answer
    // that without changing any state.
    static bool isBranchInstruction(uint32_t instruction); // B-type
    static bool isJumpInstruction(uint32_t instruction);   // JAL or JALR
    /// pipeline_memory will redirect to a taken B-type's target.
    bool branchRedirectsNextStep() const;
    /// pipeline_execute will redirect for a JAL/JALR (unless the branch flushed it).
    bool jumpRedirectsNextStep() const;
    /// The hazard unit will stall: the same check_data_hazard call Step()
    /// makes, on the registers as Step() will have left them at that point.
    bool hazardUnitWillStall(bool forwarding) const;
    /// The control signals of the instruction labelled ID, decoded on a
    /// fresh control unit - never control_unit_. A NOP decodes to all
    /// false, as pipeline_decode treats it.
    RV5SControlUnit idStageControl() const;

  public:
    /// The four pipeline registers, IF/ID to MEM/WB: the instructions the
    /// ID, EX, MEM and WB labels show.
    QList<StageCell> stageCells() const override;

  protected:
    void Run() override; // run debug run adn reset are same across all rv5s vms
    bool StepAndNotify() override;

    // --- Private methods for each pipeline stage ---
    virtual void pipeline_fetch() = 0;

    void redirect_pc(uint64_t target);
    void fetch_and_advance_pc(bool stalled);

    void pipeline_decode();

    virtual void pipeline_execute() = 0;
    virtual uint64_t pipeline_execute_float() = 0;
    virtual uint64_t pipeline_execute_double() = 0;

    uint64_t execute_float();
    uint64_t execute_double();

    /// auipc's result: the instruction's own PC plus its upper immediate,
    /// sign-extended from 32 bits (ImmGenerator hands back the raw 20 bits).
    /// The ALU cannot produce it - the control unit gives auipc no PC
    /// operand and its "register" fields are immediate bits - so each
    /// variant overrides the ALU result with this, as it already did for
    /// lui (known_issues 103).
    static uint64_t auipc_result(const ID_EX_Register &id_ex)
    {
        const auto upper = static_cast<int32_t>(static_cast<uint32_t>(id_ex.imm) << 12);
        return id_ex.pc + static_cast<uint64_t>(static_cast<int64_t>(upper));
    }

    /// The F/D op in EX hands its exception flags here; pipeline_execute
    /// latches them into EX/MEM, and they are written at writeback.
    void latch_fp_status(uint8_t status)
    {
        ex_fp_status_ = status;
        ex_fp_status_valid_ = true;
    }
    uint8_t ex_fp_status_ = 0;
    bool ex_fp_status_valid_ = false;

    void pipeline_memory();
    void pipeline_writeback();
    void pipeline_writeback_float();
    void pipeline_writeback_double();

    /// An ecall/ebreak has reached writeback: serve it, then flush and
    /// refetch everything younger (known_issues 66).
    void serve_syscall_at_writeback();

    /// A CSR instruction has reached writeback: everything older has
    /// retired. Read and write the CSR, give rd the old value, then flush
    /// and refetch everything younger, like an ecall (known_issues 104).
    void serve_csr_at_writeback();
};
}//namespace Atlas