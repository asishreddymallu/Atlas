/**
 * @file vm_base.cpp
 * @brief File containing the base class for the virtual machine
 * @author Vishank Singh, https://VishankSingh
 */

#include "processor/processor_base.h"

#include <QDeadlineTimer>
#include <QThread>
#include <QtGlobal>
#include "common/globals.h"
#include "config/config.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <thread>

namespace Atlas
{
ProcessorBase::ProcessorBase()
{
}

void ProcessorBase::MarkRunEntered()
{
    QMutexLocker locker(&run_state_mutex_);
    if (worker_in_run_)
        return; // BeginRun already approved this run and cleared the stop
    // Direct entry: ProcessorManager::run() (the REPL and the batch CLI),
    // the vm->DebugRun() tests, the out-of-tree harnesses. Nothing checked
    // an epoch for us, so a stop left standing by the previous
    // StopAndReset is still set and the loop would exit without executing
    // anything. Clear it here - same lock, same helper, one mechanism.
    worker_in_run_ = true;
    run_thread_ = QThread::currentThreadId();
    ClearStopLocked();
}

void ProcessorBase::MarkRunExited()
{
    QMutexLocker locker(&run_state_mutex_);
    worker_in_run_ = false;
    run_thread_ = nullptr;
    run_idle_condition_.wakeAll();
}

quint64 ProcessorBase::CurrentStopEpoch()
{
    QMutexLocker locker(&run_state_mutex_);
    return stop_epoch_;
}

bool ProcessorBase::BeginRun(quint64 epoch)
{
    QMutexLocker locker(&run_state_mutex_);
    if (stop_in_progress_ > 0)
        return false; // a stop is in flight: it asked first
    if (epoch != stop_epoch_)
        return false; // a stop happened after this run was requested
    // Approve, mark busy and clear the previous stop in ONE critical
    // section. StopAndReset publishes stop_requested_ under this same
    // mutex, so the two cannot interleave: a stop is either seen by the
    // epoch check above (and cancels this run) or lands after the clear
    // below (and stands). The clear used to happen later, in ClearStop(),
    // with the lock dropped in between - a stop arriving in that window
    // was wiped and the worker then parked in the untimed pause wait.
    worker_in_run_ = true;
    run_thread_ = QThread::currentThreadId();
    ClearStopLocked();
    return true;
}

bool ProcessorBase::WaitForRunIdle(int timeout_ms)
{
    QDeadlineTimer deadline(timeout_ms);
    QMutexLocker locker(&run_state_mutex_);
    while (worker_in_run_)
    {
        if (!run_idle_condition_.wait(&run_state_mutex_, deadline))
            return false; // timed out; caller must NOT clear state
    }
    return true;
}

bool ProcessorBase::StopAndReset(int timeout_ms)
{
    return StopWorker(timeout_ms, true);
}

bool ProcessorBase::StopKeepingState(int timeout_ms)
{
    return StopWorker(timeout_ms, false);
}

bool ProcessorBase::ProgramEnded() const
{
    return (exit_cycle_ && cycle_s_ >= *exit_cycle_) || RanOffTheEnd();
}

bool ProcessorBase::StopWorker(int timeout_ms, bool reset)
{
    // 1. Claim the processor and invalidate any run that was requested
    //    before this stop. Under run_state_mutex_, so it is ordered
    //    against BeginRun: either the run marked itself busy first (and
    //    step 3 waits for it), or it sees the new epoch - or the claim -
    //    and refuses to start.
    //    The stop flag is published in that SAME critical section, which
    //    is what orders it against BeginRun's clear. Publishing it
    //    outside was the residual hang: BeginRun (then ClearStop) could
    //    pass its check, drop the lock, and only afterwards wipe a stop
    //    that had been set in between.
    //
    //    The flag is atomic, but that is NOT what makes the WAKE safe:
    //    each waiter evaluates its predicate while holding its own mutex,
    //    and each wake below is issued while holding that SAME mutex, so
    //    it cannot slip between a waiter's predicate check and its wait.
    //    Measured, not assumed.
    {
        QMutexLocker locker(&run_state_mutex_);
        ++stop_epoch_;
        stop_requested_ = true;
        ++stop_in_progress_;
    } // run_state_mutex_ released HERE, before any other lock is taken

    // 2. Wake every place the worker can be blocked:
    //    - the pause wait and the step-delay wait, both on
    //      pause_wait_condition_ (and clear pause_requested_, or a paused
    //      worker would go straight back to sleep);
    //    - the ecall input wait on input_cv_, whose predicate now also
    //      tests stop_requested_.
    {
        QMutexLocker locker(&pause_mutex_);
        pause_requested_ = false;
        pause_wait_condition_.wakeAll();
    } // pause_mutex_ released HERE - see the lock-order note in the header
    {
        std::lock_guard<std::mutex> lock(input_mutex_);
        input_cv_.notify_all();
    }

    // 3. Wait until the worker has actually left Run(). One wake is
    //    enough now, and that is the point of this whole change.
    //
    //    This used to re-assert the stop and re-wake every 50 ms for the
    //    whole timeout, because a run approved just before the stop
    //    could wipe stop_requested_ in ClearStop() and then park in the
    //    untimed pause wait. That was a mask over two holes, both now
    //    closed by the critical section above: the clear happens inside
    //    BeginRun under run_state_mutex_, and stop_in_progress_ refuses
    //    a run dequeued while this stop is still in flight. With the
    //    mask removed and the holes open the probe measured 126 and 119
    //    timeouts in 500 debug-run cycles; with them closed, 0.
    //
    //    Bounded: on timeout we return false and clear nothing.
    const bool idle = WaitForRunIdle(timeout_ms);
    if (!idle)
    {
        EndStopInProgress();
        return false;
    }

    // 4. The worker is idle. step_mutex_ additionally excludes a manual
    //    Step() driven from the UI thread. The two mutexes are taken one
    //    after the other, never nested - see the header's lock order.
    if (reset)
    {
        QMutexLocker stepLock(&step_mutex_);
        Reset();
    }
    EndStopInProgress();
    return true;
}

void ProcessorBase::EndStopInProgress()
{
    QMutexLocker locker(&run_state_mutex_);
    if (stop_in_progress_ > 0)
        --stop_in_progress_;
}

bool ProcessorBase::StopFromRunLoop()
{
    {
        QMutexLocker locker(&run_state_mutex_);
        if (!worker_in_run_ || run_thread_ != QThread::currentThreadId())
            return false; // somebody else's thread: this is not our path
        // Same publish as StopAndReset, same critical section, so a run
        // request that is still queued is cancelled by this stop too.
        ++stop_epoch_;
        stop_requested_ = true;
    }
    // The loop may be sitting in the pause wait (a breakpoint, or a debug
    // run); wake it so it can see the flag. There is nothing to wait for
    // and nothing to reset - we ARE the loop, and it is about to exit.
    QMutexLocker locker(&pause_mutex_);
    pause_requested_ = false;
    pause_wait_condition_.wakeAll();
    return true;
}

void ProcessorBase::RequestStop()
{
    // The caller is ProcessorManager::step(), on the UI thread, when the
    // program ran out while the user was stepping. That one wants the
    // full stop-and-reset.
    //
    // A caller on the WORKER thread, inside Run(), cannot wait for itself
    // to leave Run() - it would burn the whole timeout every time, which
    // measured at 3016 ms per ecall - and must not Reset() under its own
    // feet. That used to be RV5StageProcessorNHNF::handle_syscall(); the
    // exit service now calls StopForProgramExit() instead (known_issues
    // 66, 102), but the guard stays for any future run-loop caller.
    if (StopFromRunLoop())
        return;

    // Was: lock pause_mutex_, set the flag, wake, and call Reset() while
    // still holding the lock. That reset ran on the UI thread underneath
    // a live worker (known_issues 62) and pinned a lock order that made
    // the obvious fix deadlock.
    if (!StopAndReset())
    {
        qWarning("Kites: the processor did not stop within the timeout; "
                 "state left untouched rather than cleared under a running worker");
    }
}
void ProcessorBase::RequestPause()
{
    pause_requested_ = true;
}
void ProcessorBase::RequestResume()
{
    QMutexLocker locker(&pause_mutex_);
    pause_requested_ = false;
    pause_wait_condition_.wakeAll();
}
bool ProcessorBase::IsStopRequested() const
{
    return stop_requested_;
}
void ProcessorBase::ClearStopLocked()
{
    // run_state_mutex_ is held by the caller. That is the whole point:
    // the clear has to be ordered against StopAndReset's publish, or a
    // stop issued between the two is silently lost.
    stop_requested_ = false;
    pause_requested_ = start_paused_.exchange(false);
}

void ProcessorBase::ClearStop()
{
    // Run()/DebugRun() no longer call this - RunScope's MarkRunEntered
    // does the clearing, on the same lock as the epoch check. The only
    // caller left is ProcessorManager::debugRun(), which arms the REPL's
    // manual-step mode without starting a run loop at all.
    QMutexLocker locker(&run_state_mutex_);
    ClearStopLocked();
}

void ProcessorBase::LoadProgram(const AssembledProgram &program)
{
    program_ = program;
    exit_cycle_.reset();
    unsigned int counter = 0;
    for (const auto &instruction : program.text_buffer)
    {
        memory_controller_.writeWord_d(counter, instruction);
        counter += 4;
    }
    program_size_ = counter;
    AddBreakpoint(program_size_, false); // address

    unsigned int data_counter = 0;
    uint64_t base_data_address = vm_config::config.getDataSectionStart();
    auto align = [&](unsigned int alignment)
    {
        if (data_counter % alignment != 0)
            data_counter += alignment - (data_counter % alignment);
    };
    for (const auto &data : program.data_buffer)
    {
        std::visit(
            [&](auto &&value)
            {
                using T = std::decay_t<decltype(value)>;

                if constexpr (std::is_same_v<T, uint8_t>)
                {
                    align(1);
                    memory_controller_.writeByte_d(base_data_address + data_counter,
                                                   value); // Write a byte
                    data_counter += 1;
                }
                else if constexpr (std::is_same_v<T, uint16_t>)
                {
                    align(2);
                    memory_controller_.writeHalfWord_d(base_data_address + data_counter,
                                                       value); // Write a halfword (16 bits)
                    data_counter += 2;
                }
                else if constexpr (std::is_same_v<T, uint32_t>)
                {
                    align(4);
                    memory_controller_.writeWord_d(base_data_address + data_counter,
                                                   value); // Write a word (32 bits)
                    data_counter += 4;
                }
                else if constexpr (std::is_same_v<T, uint64_t>)
                {
                    align(8);
                    memory_controller_.writeDoubleWord_d(base_data_address + data_counter,
                                                         value); // Write a double word (64 bits)
                    data_counter += 8;
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    align(4);
                    uint32_t float_as_int;
                    std::memcpy(&float_as_int, &value, sizeof(float));
                    memory_controller_.writeWord_d(base_data_address + data_counter,
                                                   float_as_int); // Write the float as a word
                    data_counter += 4;
                }
                else if constexpr (std::is_same_v<T, double>)
                {
                    align(8);
                    uint64_t double_as_int;
                    std::memcpy(&double_as_int, &value, sizeof(double));
                    memory_controller_.writeDoubleWord_d(
                        base_data_address + data_counter,
                        double_as_int); // Write the double as a double word
                    data_counter += 8;
                }
                else if constexpr (std::is_same_v<T, std::string>)
                {
                    align(1);
                    for (size_t i = 0; i < value.size(); i++)
                    {
                        memory_controller_.writeByte_d(
                            base_data_address + data_counter,
                            static_cast<uint8_t>(value[i])); // Write each byte of the string
                        data_counter += 1;
                    }
                }
            },
            data);
    }

    DumpState(globals::vm_state_dump_file_path);
}

uint64_t ProcessorBase::GetProgramCounter() const
{
    return program_counter_;
}

void ProcessorBase::UpdateProgramCounter(int64_t value)
{
    program_counter_ = static_cast<uint64_t>(static_cast<int64_t>(program_counter_) + value);
}

auto sign_extend = [](uint32_t value, unsigned int bits) -> int32_t
{
    int32_t mask = 1 << (bits - 1);
    return (value ^ mask) - mask;
};

int32_t ProcessorBase::ImmGenerator(uint32_t instruction)
{
    int32_t imm = 0;
    uint8_t opcode = instruction & 0b1111111;

    switch (opcode)
    {
    /*** I-TYPE (Load, alu Immediate, JALR, FPU Loads) ***/
    case 0b0010011: // alu Immediate (ADDI, SLTI, SLTIU, XORI, ORI, ANDI, SLLI, SRLI, SRAI)
    case 0b0011011: // OP-IMM-32 (ADDIW, SLLIW, SRLIW, SRAIW) - missing until known_issues
                    // 109, so every W immediate read as 0 on all five models
    case 0b0000011: // Load (LB, LH, LW, LD, LBU, LHU, LWU)
    case 0b1100111: // JALR
    case 0b0001111: // FENCE
    case 0b0000111: // FLW, FLD (Floating-point load)
        imm = (instruction >> 20) & 0xFFF;
        imm = sign_extend(imm, 12);
        break;

    /*** S-TYPE (Store, Floating-Point Store) ***/
    case 0b0100011: // Store (SB, SH, SW, SD)
    case 0b0100111: // FSW, FSD (Floating-point store)
        imm = ((instruction >> 7) & 0x1F) | ((instruction >> 25) & 0x7F) << 5;
        imm = sign_extend(imm, 12);
        break;

    /*** SB-TYPE (branch_ Instructions) ***/
    case 0b1100011:                                // branch_ (BEQ, BNE, BLT, BGE, BLTU, BGEU)
        imm = ((instruction >> 8) & 0xF)           // Bits 11:8
              | ((instruction >> 25) & 0x3F) << 4  // Bits 10:5
              | ((instruction >> 7) & 0x1) << 10   // Bit 4
              | ((instruction >> 31) & 0x1) << 11; // Bit 12
        imm <<= 1;
        imm = sign_extend(imm, 13);
        break;

    /*** U-TYPE (LUI, AUIPC) ***/
    case 0b0110111:                             // LUI
    case 0b0010111:                             // AUIPC
        imm = (instruction & 0xFFFFF000) >> 12; // Upper 20 bits

        break;

    /*** J-TYPE (JAL) ***/
    case 0b1101111:                                // JAL
        imm = ((instruction >> 21) & 0x3FF)        // Bits 10:1
              | ((instruction >> 20) & 0x1) << 10  // Bit 11
              | ((instruction >> 12) & 0xFF) << 11 // Bits 19:12
              | ((instruction >> 31) & 0x1) << 19; // Bit 20
        imm <<= 1;                                 // Shift left by 1
        // if (imm & 0x1000) {
        //    imm |= 0xFFFFE000;
        // }
        imm = sign_extend(imm, 21); // Might be 20
        break;

    /*** M-EXTENSION (Multiplication, Division) - R-TYPE ***/
    case 0b0110011: // kMul, kMulh, kMulhu, kMulhsu, kDiv, kDivu, kRem, kRemu
        // R-Type (no immediate needed)
        imm = 0;
        break;

    /*** F-EXTENSION (Floating Point Operations) - R-TYPE ***/
    case 0b1010011: // Floating-point (FADD, FSUB, FMUL, FDIV, FSQRT, etc.)
        // R-Type (no immediate needed)
        imm = 0;
        break;

    default:
        imm = 0;
        break;
    }

    return imm;
}

void ProcessorBase::SetBreakpoints(const std::vector<uint64_t> &breakpoints)
{
    breakpoints_.clear();
    for (const auto &bp : breakpoints)
    {
        breakpoints_.emplace_back(program_.line_number_instruction_number_mapping[bp] * 4);
    }
}

void ProcessorBase::AddBreakpoint(uint64_t val, bool is_line)
{
    if (is_line)
    {
        // If the value is a line number, convert it to an instruction address
        if (program_.line_number_instruction_number_mapping.find(val) ==
            program_.line_number_instruction_number_mapping.end())
        {
            std::cerr << "Invalid line number: " << val << std::endl;
            return;
        }
        uint64_t line = val;
        uint64_t bp = program_.line_number_instruction_number_mapping[line] * 4;
        if (CheckBreakpoint(bp))
        {
            std::cerr << "Breakpoint already exists at line: " << line << std::endl;
            return;
        }
        breakpoints_.emplace_back(bp);
    }
    else
    {
        if (val % 4 != 0)
        {
            std::cerr << "Invalid instruction address: " << val << ". Must be a multiple of 4."
                      << std::endl;
            return;
        }
        if (CheckBreakpoint(val))
        {
            std::cerr << "Breakpoint already exists at address: " << val << std::endl;
            return;
        }
        breakpoints_.emplace_back(val);
    }

    DumpState(globals::vm_state_dump_file_path);
}

void ProcessorBase::RemoveBreakpoint(uint64_t val, bool is_line)
{
    if (is_line)
    {
        // If the value is a line number, convert it to an instruction address
        if (program_.line_number_instruction_number_mapping.find(val) ==
            program_.line_number_instruction_number_mapping.end())
        {
            std::cerr << "Invalid line number: " << val << std::endl;
            return;
        }
        uint64_t line = val;
        uint64_t bp = program_.line_number_instruction_number_mapping[line] * 4;
        if (!CheckBreakpoint(bp))
        {
            std::cerr << "No breakpoint exists at line: " << line << std::endl;
            return;
        }
        breakpoints_.erase(std::remove(breakpoints_.begin(), breakpoints_.end(), bp),
                           breakpoints_.end());
    }
    else
    {
        if (val % 4 != 0)
        {
            std::cerr << "Invalid instruction address: " << val << ". Must be a multiple of 4."
                      << std::endl;
            return;
        }
        if (!CheckBreakpoint(val))
        {
            std::cerr << "No breakpoint exists at address: " << val << std::endl;
            return;
        }
        breakpoints_.erase(std::remove(breakpoints_.begin(), breakpoints_.end(), val),
                           breakpoints_.end());
    }
    DumpState(globals::vm_state_dump_file_path);
}

bool ProcessorBase::CheckBreakpoint(uint64_t address)
{
    return std::find(breakpoints_.begin(), breakpoints_.end(), address) != breakpoints_.end();
}

// The system-call service every model shares (known_issues 66). Moved here
// verbatim from RVSSProcessor::HandleSyscall, so the five models cannot
// drift apart; the only change is the exit service (see SYSCALL_EXIT).
// The caller decides WHEN this runs - rvss in Execute, the pipelines when
// the ecall reaches writeback - and passes the delta the step records into.
bool ProcessorBase::ServeSyscall(StepDelta &delta)
{
    uint64_t syscall_number = registers_.ReadGpr(17);
    switch (syscall_number)
    {
    case SYSCALL_PRINT_INT:
    {
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        std::cout << static_cast<int64_t>(registers_.ReadGpr(10)); // Print signed integer
        EmitProgramOutput(std::to_string(static_cast<int64_t>(registers_.ReadGpr(10))));
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_FLOAT:
    { // print float
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        float float_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&float_value, &raw, sizeof(float_value));
        std::cout << std::setprecision(std::numeric_limits<float>::max_digits10) << float_value;
        {
            std::ostringstream text;
            text << std::setprecision(std::numeric_limits<float>::max_digits10) << float_value;
            EmitProgramOutput(text.str());
        }
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_DOUBLE:
    { // print double
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        else
        {
            std::cout << "VM_STDOUT_START";
        }
        double double_value;
        uint64_t raw = registers_.ReadGpr(10);
        std::memcpy(&double_value, &raw, sizeof(double_value));
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10) << double_value;
        {
            std::ostringstream text;
            text << std::setprecision(std::numeric_limits<double>::max_digits10) << double_value;
            EmitProgramOutput(text.str());
        }
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        else
        {
            std::cout << "VM_STDOUT_END" << std::endl;
        }
        break;
    }
    case SYSCALL_PRINT_STRING:
    {
        if (!globals::vm_as_backend)
        {
            std::cout << "[Syscall output: ";
        }
        EmitProgramOutput(PrintString(registers_.ReadGpr(10))); // Print string
        if (!globals::vm_as_backend)
        {
            std::cout << "]" << std::endl;
        }
        break;
    }
    case SYSCALL_EXIT:
    {
        if (!globals::vm_as_backend)
        {
            std::cout << "VM_EXIT" << std::endl;
        }
        output_status_ = "VM_EXIT";
        std::cout << "Exited with exit code: " << registers_.ReadGpr(10) << std::endl;
        // Was exit(0), which ended the whole process - the GUI, kites-cli
        // before it dumped its registers, and the test binary - instead
        // of the simulated program (known_issues 101). Stop the program
        // and keep its state, the behaviour known_issues 67 settled on.
        // The cycle count once this step completes: the step that served
        // the exit is where the program ended (ProgramEnded).
        exit_cycle_ = cycle_s_ + 1;
        StopForProgramExit();
        return true;
    }
    case SYSCALL_READ:
    { // Read
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 0)
        {
            // Read from stdin
            std::string input;
            {
                std::cout << "VM_STDIN_START" << std::endl;
                output_status_ = "VM_STDIN_START";
                std::unique_lock<std::mutex> lock(input_mutex_);
                // Tell the window it can offer an input line. Queued to the
                // UI thread, so emitting under input_mutex_ cannot deadlock.
                if (input_queue_.empty())
                    emit inputRequestedSignal();
                // Also wakes on stop: without this a program blocked in an
                // ecall read could never be stopped, and StopAndReset would
                // sit out its whole timeout.
                input_cv_.wait(lock,
                               [this]()
                               { return !input_queue_.empty() || stop_requested_; });
                if (input_queue_.empty())
                    return false; // stopped while waiting for input
                output_status_ = "VM_STDIN_END";
                std::cout << "VM_STDIN_END" << std::endl;

                input = input_queue_.front();
                input_queue_.pop();
            }

            std::vector<uint8_t> old_bytes_vec(length, 0);
            std::vector<uint8_t> new_bytes_vec(length, 0);

            for (size_t i = 0; i < length; ++i)
            {
                old_bytes_vec[i] = memory_controller_.peekByte(buffer_address + i);
            }

            for (size_t i = 0; i < input.size() && i < length; ++i)
            {
                memory_controller_.writeByte(buffer_address + i, static_cast<uint8_t>(input[i]));
            }
            if (input.size() < length)
            {
                memory_controller_.writeByte(buffer_address + input.size(), '\0');
            }

            for (size_t i = 0; i < length; ++i)
            {
                new_bytes_vec[i] = memory_controller_.peekByte(buffer_address + i);
            }

            delta.memory_changes.push_back({buffer_address, old_bytes_vec, new_bytes_vec});

            uint64_t old_reg = registers_.ReadGpr(10);
            unsigned int reg_index = 10;
            unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR
            uint64_t new_reg =
                std::min(static_cast<uint64_t>(length), static_cast<uint64_t>(input.size()));
            registers_.WriteGpr(10, new_reg);
            if (old_reg != new_reg)
            {
                delta.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
            }
        }
        else
        {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    case SYSCALL_WRITE:
    { // Write
        uint64_t file_descriptor = registers_.ReadGpr(10);
        uint64_t buffer_address = registers_.ReadGpr(11);
        uint64_t length = registers_.ReadGpr(12);

        if (file_descriptor == 1)
        { // stdout
            std::cout << "VM_STDOUT_START";
            output_status_ = "VM_STDOUT_START";
            uint64_t bytes_printed = 0;
            std::string written;
            for (uint64_t i = 0; i < length; ++i)
            {
                char c = memory_controller_.readByte(buffer_address + i);
                written += c;
                // if (c == '\0') {
                //     break;
                // }
                std::cout << c;
                bytes_printed++;
            }
            std::cout << std::flush;
            EmitProgramOutput(written);
            output_status_ = "VM_STDOUT_END";
            std::cout << "VM_STDOUT_END" << std::endl;

            uint64_t old_reg = registers_.ReadGpr(10);
            unsigned int reg_index = 10;
            unsigned int reg_type = 0; // 0 for GPR, 1 for CSR, 2 for FPR
            uint64_t new_reg = std::min(static_cast<uint64_t>(length), bytes_printed);
            registers_.WriteGpr(10, new_reg);
            if (old_reg != new_reg)
            {
                delta.register_changes.push_back({reg_index, reg_type, old_reg, new_reg});
            }
        }
        else
        {
            std::cerr << "Unsupported file descriptor: " << file_descriptor << std::endl;
        }
        break;
    }
    default:
    {
        std::cerr << "Unknown syscall number: " << syscall_number << std::endl;
        break;
    }
    }
    return false;
}

void ProcessorBase::StopForProgramExit()
{
    // On the run thread - Run() or DebugRun(), the worker in the GUI -
    // publish the stop and let the loop leave; nothing is reset (67).
    if (StopFromRunLoop())
        return;
    // Anywhere else this is a manual step: ProcessorManager::step() on the
    // UI thread, inside StepAndNotify(), which holds step_mutex_. It must
    // NOT go through RequestStop(): StopAndReset() takes step_mutex_ again
    // and a QMutex is not recursive - that was a self-deadlock on rv5-nh-nf
    // (known_issues 102). Record the exit; StepAndNotify() reports the end
    // of the program, and the caller takes the same path as a program
    // that ran off its last instruction.
    program_exited_ = true;
}

bool ProcessorBase::TakeProgramExit()
{
    const bool exited = program_exited_;
    program_exited_ = false;
    return exited;
}

std::string ProcessorBase::PrintString(uint64_t address)
{
    std::string printed;
    while (true)
    {
        char c = memory_controller_.readByte(address);
        if (c == '\0')
            break;
        std::cout << c;
        printed += c;
        address++;
    }
    return printed;
}

void ProcessorBase::EmitProgramOutput(const std::string &text)
{
    // std::cout above stays as it was: kites-cli and the tests read it.
    // This is the window's copy (known_issues 140).
    if (!text.empty())
        emit programOutputSignal(QString::fromStdString(text));
}

bool ProcessorBase::NextStepWaitsForInput()
{
    if (!NextStepServesEcall())
        return false;
    // Every instruction older than the ecall has retired, so a7 and a0 are
    // the values the service will read.
    if (registers_.ReadGpr(17) != SYSCALL_READ || registers_.ReadGpr(10) != 0)
        return false;
    std::lock_guard<std::mutex> lock(input_mutex_);
    return input_queue_.empty();
}

bool ProcessorBase::NextStepServesEcall()
{
    // rvss: the instruction at the PC executes, ecall included, next step.
    if (program_counter_ >= program_size_)
        return false;
    uint32_t instruction = 0;
    for (int i = 3; i >= 0; --i)
        instruction = (instruction << 8) | memory_controller_.peekByte(program_counter_ + static_cast<uint64_t>(i));
    return instruction == 0x00000073u; // ecall
}

void ProcessorBase::DumpState(const std::filesystem::path &filename)
{
    std::ofstream file(filename);
    if (!file.is_open())
    {
        std::cerr << "Error opening file for dumping VM state: " << filename.string() << std::endl;
        return;
    }

    unsigned int instruction_number = program_counter_ / 4;
    unsigned int current_line = program_.instruction_number_line_number_mapping[instruction_number];

    file << "{\n";
    file << "    \"program_counter\": " << "\"0x" << std::hex << std::setw(8) << std::setfill('0')
         << program_counter_ << std::dec << std::setfill(' ') << "\",\n";
    file << "    \"current_line\": " << current_line << ",\n";
    file << "    \"current_instruction\": " << "\"0x" << std::hex << std::setw(8)
         << std::setfill('0') << current_instruction_ << std::dec << std::setfill(' ') << "\",\n";
    file << "    \"disassembly_line_number\": "
         << program_.instruction_number_disassembly_mapping[instruction_number] << ",\n";
    file << "    \"cycle_count\": " << cycle_s_ << ",\n";
    file << "    \"instructions_retired\": " << instructions_retired_ << ",\n";
    file << "    \"cpi\": " << cpi_ << ",\n";
    file << "    \"ipc\": " << ipc_ << ",\n";
    file << "    \"stall_cycles\": " << stall_cycles_ << ",\n";
    file << "    \"branch_mispredictions\": " << branch_mispredictions_ << ",\n";
    file << "    \"breakpoints\": [";
    for (size_t i = 1; i < breakpoints_.size(); ++i)
    {
        program_.instruction_number_line_number_mapping[breakpoints_[i] / 4];
        file << program_.instruction_number_line_number_mapping[breakpoints_[i] / 4];
        if (i < breakpoints_.size() - 1)
        {
            file << ", ";
        }
    }
    file << "],\n";
    file << "    \"output_status\": \"" << output_status_ << "\"\n";
    file << "}\n";
    file.close();
}

void ProcessorBase::ModifyRegister(const std::string &reg_name, uint64_t value)
{
    registers_.ModifyRegister(reg_name, value);
}

void ProcessorBase::resetWiresToBackbone()
{
    const qsizetype backbone = static_cast<qsizetype>(always_active_wires_count_);
    active_wires_.resize(backbone);
    active_wire_stages_ = QList<quint8>(backbone, kNoStage);
}

void ProcessorBase::emitCircuitState()
{
    CircuitSnapshot snapshot;
    snapshot.wires  = active_wires_;
    snapshot.stages = active_wire_stages_;
    // RVSS appends without lightWire, so its stage list can be shorter;
    // every wire past it is unstaged.
    snapshot.stages.resize(snapshot.wires.size(), kNoStage);
    snapshot.cells = stageCells();
    emit updateCircuitStateSignal(active_wires_);
    emit circuitSnapshotSignal(snapshot);
}

void ProcessorBase::lightWire(const QString &name, WireStage stage)
{
    const qsizetype at = active_wires_.indexOf(name);
    if (at < 0)
    {
        active_wires_.append(name);
        active_wire_stages_.append(stageBit(stage));
        return;
    }
    // Backbone entries stay unstaged: they are on whatever any instruction
    // does (known_issues 98), so they belong to no one stage.
    if (at >= static_cast<qsizetype>(always_active_wires_count_))
    {
        active_wire_stages_[at] |= stageBit(stage);
    }
}
}//namespace Atlas
