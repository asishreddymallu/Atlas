/**
 * @file vm_base.h
 * @brief File containing the base class for the virtual machine
 * @author Vishank Singh, https://VishankSingh
 */
#ifndef VM_BASE_H
#define VM_BASE_H

#include "alu.h"
#include "common/assembled_program.h"
#include "memory_controller.h"
#include "processor/processor_state.h"
#include "processor/registers.h"
#include "ui/processor_tab/circuit_scene.h"
#include "processor/processor_constants.h"
#include "common/undo_buffer.h"
#include <QList>
#include <QMap>
#include <QMutex>
#include <QString>
#include <QWaitCondition>
#include <QWidget>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <queue>
#include <stack>
#include <string>
#include <vector>

namespace Kites
{

enum SyscallCode
{
    SYSCALL_PRINT_INT    = 1,
    SYSCALL_PRINT_FLOAT  = 2,
    SYSCALL_PRINT_DOUBLE = 3,
    SYSCALL_PRINT_STRING = 4,
    SYSCALL_EXIT         = 10,
    SYSCALL_READ         = 63,
    SYSCALL_WRITE        = 64,
};

struct RegisterChange
{
    unsigned int reg_index;
    unsigned int reg_type; // 0 for GPR, 1 for CSR, 2 for FPR
    uint64_t old_value;
    uint64_t new_value;
};

struct MemoryChange
{
    uint64_t address;
    std::vector<uint8_t> old_bytes_vec;
    std::vector<uint8_t> new_bytes_vec;
};

struct StepDelta
{
    uint64_t old_pc{};
    uint64_t new_pc{};
    unsigned int old_cycle{};
    unsigned int new_cycle{};
    unsigned int old_instructions_retired{};
    unsigned int new_instructions_retired{};
    unsigned int old_stall_cycles{};
    unsigned int new_stall_cycles{};
    unsigned int old_branch_mispredictions{};
    unsigned int new_branch_mispredictions{};
    std::vector<RegisterChange> register_changes{};
    std::vector<MemoryChange> memory_changes{};
};

/**
 * @brief Base class for the virtual machine.
 */
class ProcessorBase : public QObject
{
    Q_OBJECT
  public:
    ProcessorBase();
    ~ProcessorBase() = default;

    AssembledProgram program_;
    std::atomic<bool> stop_requested_ {false};
    std::atomic<bool> pause_requested_ {false};
    // one shot flag, when set the next ClearStop() leaves the processor paused (used by debug run)
    std::atomic<bool> start_paused_ {false};

    QMutex pause_mutex_;
    QWaitCondition pause_wait_condition_;
    // serializes cycles between the run thread and manual steps from the gui thread
    QMutex step_mutex_;

    // ─────────────────── THREADING INVARIANT ───────────────────────────
    // The processor object lives on a worker QThread (main_window.cpp
    // moves ProcessorManager to it), but the UI thread calls into it
    // directly: reset(), stop(), undo(), redo(), setBreakpoints(). Before
    // known_issues 62 was fixed, Reset() cleared the caches and the
    // undo/redo stacks from the UI thread while the worker was inside
    // Run(); that is what corrupted the heap and tripped the cache
    // assertion.
    //
    // LOCK ORDER, and it is a one-way street:
    //
    //      run_state_mutex_   ->   step_mutex_      (never both held)
    //      run_state_mutex_   ->   pause_mutex_     (sequential, never nested)
    //      run_state_mutex_   ->   input_mutex_     (sequential, never nested)
    //
    //   * The UI thread takes run_state_mutex_ to publish the stop and to
    //     wait until the worker has left Run(), RELEASES it, and only
    //     then takes step_mutex_ to clear state. It never holds both.
    //   * StopAndReset takes run_state_mutex_, releases it, and only
    //     then takes pause_mutex_ and input_mutex_ in turn. No two of
    //     the four are ever held at the same time by anyone.
    //   * The worker takes step_mutex_ inside StepAndNotify(), and
    //     pause_mutex_ separately before and after each step. It NEVER
    //     holds step_mutex_ while acquiring pause_mutex_ or
    //     run_state_mutex_, so no cycle can form.
    //   * RequestStop() must NOT hold pause_mutex_ while resetting. It
    //     used to, which put the UI thread's pause_mutex_ -> (Reset)
    //     ordering up against the worker's, and was the lock-ordering
    //     hazard that made a naive fix deadlock.
    //
    // worker_in_run_ is true for exactly as long as a Run()/DebugRun()
    // loop is executing on the worker; RunScope maintains it.
    QMutex run_state_mutex_;
    QWaitCondition run_idle_condition_;
    bool worker_in_run_{false};
    // The thread worker_in_run_ was set on, i.e. the thread currently
    // inside Run(). RequestStop() compares against it: a stop asked for
    // BY the run loop (rv5s ecall exit) cannot wait for that same loop
    // to finish, and must not Reset() under its own feet.
    Qt::HANDLE run_thread_{nullptr};
    // Bumped by StopAndReset under run_state_mutex_, in the SAME critical
    // section that publishes stop_requested_. A run carries the epoch it
    // was REQUESTED under; if a stop has happened since, the run refuses
    // to start. Checking the epoch, marking the worker busy AND clearing
    // the previous stop all happen in that one critical section
    // (BeginRun), so a stop either lands before the run is approved - and
    // cancels it - or after the clear, and survives. There is no third
    // outcome, which is what the old check-then-act ClearStop allowed:
    // measured at 47 hangs in 60 debug-run cycles with a 100 ms delay
    // injected into that window.
    quint64 stop_epoch_{0};
    // Non-zero for as long as a StopAndReset is between publishing the
    // stop and finishing its Reset(). BeginRun refuses while it is set,
    // whatever the epoch says.
    //
    // The epoch alone is not enough for a run REQUESTED on the worker
    // itself: ProcessorManager::runSlot() reads CurrentStopEpoch() at
    // dequeue time, so its epoch always matches and the run is always
    // approved - including one dequeued in the middle of a stop, which
    // then clears the stop and runs to completion while StopAndReset
    // waits. Measured at 126 timeouts in 500 debug-run cycles once the
    // re-assert loop that had been hiding it was removed.
    int stop_in_progress_{0};

    std::mutex input_mutex_;
    std::condition_variable input_cv_;
    std::queue<std::string> input_queue_;

    std::vector<uint64_t> breakpoints_;
    std::optional<uint64_t> last_breakpoint_pc_{std::nullopt}; // Track last PC where we paused at breakpoint
    // This is to prevent multiple pause signals when we are paused at a breakpoint and the user
    // tries to resume but the PC hasn't moved yet

    uint32_t current_instruction_{};
    uint64_t program_counter_{};
    uint64_t last_executed_pc_{INVALID_PC}; 
    ProcessorState processor_state_{};

    unsigned int step_delay_{1000}; // well change it later to get it from config
    unsigned int cycle_s_{};
    unsigned int instructions_retired_{};
    double cpi_{};
    double ipc_{};
    unsigned int stall_cycles_{};
    unsigned int branch_mispredictions_{};

    std::string output_status_;

    MemoryController memory_controller_;
    RegisterFile registers_;

    alu::Alu alu_;

    QList<QString> active_wires_{};
    size_t always_active_wires_count_{};
    // the list of wire that will be active in this cycle of vm
    // well send this to the gui to highlight those wires


    std::unique_ptr<CircuitScene> circuit_scene_; // Circuit scene for visualization
    UndoBuffer<StepDelta> m_undoBuffer{100};
    void LoadProgram(const AssembledProgram &program);
    uint64_t program_size_ = 0;

    uint64_t GetProgramCounter() const;
    void UpdateProgramCounter(int64_t value);

    int32_t ImmGenerator(uint32_t instruction);

    /**@brief Sets the breakpoints vector
     */
    void SetBreakpoints(const std::vector<uint64_t> &breakpoints);
    void AddBreakpoint(uint64_t val, bool is_line = true);
    void RemoveBreakpoint(uint64_t val, bool is_line = true);
    bool CheckBreakpoint(uint64_t address);

    void RequestStop();
    void RequestPause();
    void RequestResume();

    /**@brief Entry point for a Run()/DebugRun() reached WITHOUT BeginRun
     * (ProcessorManager::run(), the REPL, the tests, the out-of-tree
     * harnesses). Marks the worker busy and clears a stale stop - the
     * same work BeginRun does, through the same ClearStopLocked, so
     * there is exactly one clearing mechanism. A no-op when BeginRun has
     * already approved this run. Use RunScope, do not call directly.
     */
    void MarkRunEntered();
    void MarkRunExited();

    /**@brief The epoch a run should be requested under (UI thread). */
    quint64 CurrentStopEpoch();

    /**@brief Atomically: has a stop happened since `epoch`? If not, mark
     * the worker busy and return true. If yes, return false and the
     * caller must NOT run - the request was overtaken by a stop.
     */
    bool BeginRun(quint64 epoch);

    /**@brief Blocks the CALLING thread until no Run() loop is executing.
     * @return false on timeout, in which case the caller must not clear state.
     */
    bool WaitForRunIdle(int timeout_ms);

    /**@brief Stops the worker, waits for it to leave Run(), then resets.
     *
     * The whole of known_issues 62 in one call. Returns false if the
     * worker did not become idle within timeout_ms; on false NOTHING is
     * cleared, because a hung UI is better than a corrupted processor.
     */
    bool StopAndReset(int timeout_ms = 3000);

    /**@brief The stop path for a caller that IS the run loop.
     *
     * RV5StageProcessorNHNF::handle_syscall() calls RequestStop() from
     * inside Run(), on the worker's own thread. Waiting for that thread
     * to leave Run() can only time out - measured at 3016 ms per ecall -
     * and resetting under it would wipe the program mid-run. This
     * publishes the stop and wakes the waits, and does nothing else.
     *
     * @return false if the caller is NOT the run thread, in which case
     * nothing was done and the caller should use StopAndReset().
     */
    bool StopFromRunLoop();

    /// RAII marker for a Run()/DebugRun() loop body.
    struct RunScope
    {
        explicit RunScope(ProcessorBase &vm) : vm_(vm) { vm_.MarkRunEntered(); }
        ~RunScope() { vm_.MarkRunExited(); }
        RunScope(const RunScope &) = delete;
        RunScope &operator=(const RunScope &) = delete;
        ProcessorBase &vm_;
    };

    bool IsStopRequested() const;
    void ClearStop();

  private:
    /**@brief THE place a stop is cleared. run_state_mutex_ must be held,
     * so that the clear is ordered against StopAndReset's publish.
     */
    void ClearStopLocked();

    /**@brief Drops this StopAndReset's claim on the processor. */
    void EndStopInProgress();

  public:
    virtual void SetActiveWireNames() = 0;
    virtual void setProcessorState() = 0;

    void PrintString(uint64_t address);

    virtual void Run()      = 0;
    virtual void DebugRun() = 0;
    virtual void Step()     = 0;
    /**@brief Executes one step and notifies the ui (circuit, highlights, registers).
     * @return true if the program still has work remaining after this step
     */
    virtual bool StepAndNotify() = 0;
    virtual void Undo()     = 0;
    virtual void Redo()     = 0;
    virtual void Reset()    = 0;
    void DumpState(const std::filesystem::path &filename);

    void ModifyRegister(const std::string &reg_name, uint64_t value);
    void PushInput(const std::string &input)
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_queue_.push(input);
        input_cv_.notify_one();
    }
  signals:
    // vm state will have all the info like pc,cycles, control signals
    void processorClockedSignal(const ProcessorState &processorState);
    // this will send the list of wire that have to
    void updateCircuitStateSignal(const QList<QString> &wireList);
    // we emit this signal when vm is paused at breakpoint
    //  so that gui can chane paused button to resume
    void processorPausedAtBreakpointSignal();
};

}  // namespace Kites

#endif // VM_BASE_H
