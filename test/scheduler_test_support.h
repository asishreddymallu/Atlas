/**
 * @file scheduler_test_support.h
 * @brief A test-side oracle for the reordering scheduler: run a program to
 *        halt on one model and compare two runs' final architectural state.
 *
 * Deliberately independent of the product's safety gate
 * (src/scheduler_gate/), so the differential tests do not check the gate
 * with itself.
 *
 * "Memory" here is every byte either run wrote, taken from the RV5S
 * per-step undo deltas, each read back with peekByte() (through the
 * caches, as the program would see it). Both runs start from the same
 * loaded image, so a byte neither run wrote is equal in both.
 */
#ifndef ATLAS_TEST_SCHEDULER_TEST_SUPPORT_H
#define ATLAS_TEST_SCHEDULER_TEST_SUPPORT_H

#include "float_test_support.h"

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_base.h"

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <stack>
#include <string>

namespace Atlas::test_support::scheduler
{
/// The demos shipped with the scheduler; tests run from test/.
inline std::string readDemo(const std::string &name)
{
    std::ifstream in("../examples/scheduler/" + name);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

inline AssembledProgram assembleText(const std::string &text)
{
    std::istringstream in(text);
    return assemble(in);
}

struct FinalState
{
    bool halted{false};
    unsigned int cycles{0};
    std::array<uint64_t, 32> x{};
    std::array<uint64_t, 32> f{};
    std::set<uint64_t> written;
    std::unique_ptr<ProcessorBase> vm;
};

/// A five-stage model with its (protected) per-step undo deltas readable,
/// which is where every store's address and bytes are recorded. A test-side
/// subclass rather than a change to the processor.
template <typename Vm> struct Observable : Vm
{
    using Vm::undo_stack_;
};

inline std::unique_ptr<ProcessorBase> makeObservable(Model model)
{
    switch (model)
    {
    case Model::Rv5NhNf: return std::make_unique<Observable<RV5StageProcessorNHNF>>();
    case Model::Rv5NhF:  return std::make_unique<Observable<RV5StageProcessorNHF>>();
    case Model::Rv5HNf:  return std::make_unique<Observable<RV5StageProcessorHNF>>();
    case Model::Rv5HF:   return std::make_unique<Observable<RV5StageProcessorHF>>();
    case Model::Rvss:    break;
    }
    return makeProcessor(model);
}

/// The undo deltas of an Observable five-stage model; empty for rvss.
inline std::stack<RV5StageStepDelta> undoDeltas(ProcessorBase &vm)
{
    if (auto *p = dynamic_cast<Observable<RV5StageProcessorHF> *>(&vm))   return p->undo_stack_;
    if (auto *p = dynamic_cast<Observable<RV5StageProcessorHNF> *>(&vm))  return p->undo_stack_;
    if (auto *p = dynamic_cast<Observable<RV5StageProcessorNHF> *>(&vm))  return p->undo_stack_;
    if (auto *p = dynamic_cast<Observable<RV5StageProcessorNHNF> *>(&vm)) return p->undo_stack_;
    return {};
}

/// Assembles `text`, loads it on `model` and steps it until the program
/// halts (or exits), or `cap` cycles have passed.
inline FinalState runToHalt(Model model, const std::string &text, unsigned int cap = 10000)
{
    FinalState state;
    setupVmStateDirectory();
    state.vm = makeObservable(model);
    state.vm->LoadProgram(assembleText(text));
    while (state.vm->cycle_s_ < cap)
    {
        if (!state.vm->StepAndNotify())
        {
            state.halted = true;
            break;
        }
    }
    state.cycles = state.vm->cycle_s_;
    for (unsigned int i = 0; i < 32; ++i)
    {
        state.x[i] = state.vm->registers_.ReadGpr(i);
        state.f[i] = state.vm->registers_.ReadFpr(i);
    }
    for (std::stack<RV5StageStepDelta> deltas = undoDeltas(*state.vm); !deltas.empty();
         deltas.pop())
        for (const MemoryChange &change : deltas.top().memory_changes)
            for (uint64_t i = 0; i < change.new_bytes_vec.size(); ++i)
                state.written.insert(change.address + i);
    return state;
}

/// "" when both runs halted with identical registers and memory, otherwise
/// the first difference found.
inline std::string compareFinalState(FinalState &a, FinalState &b)
{
    if (!a.halted || !b.halted)
        return "a run did not halt";
    for (unsigned int i = 0; i < 32; ++i)
    {
        if (a.x[i] != b.x[i])
            return "x" + std::to_string(i) + ": " + hex(a.x[i], 16) + " vs " + hex(b.x[i], 16);
        if (a.f[i] != b.f[i])
            return "f" + std::to_string(i) + ": " + hex(a.f[i], 16) + " vs " + hex(b.f[i], 16);
    }
    std::set<uint64_t> addresses = a.written;
    addresses.insert(b.written.begin(), b.written.end());
    for (uint64_t address : addresses)
    {
        const uint8_t left  = a.vm->memory_controller_.peekByte(address);
        const uint8_t right = b.vm->memory_controller_.peekByte(address);
        if (left != right)
            return "memory[" + hex(address, 8) + "]: " + hex(left, 2) + " vs " + hex(right, 2);
    }
    return "";
}
} // namespace Atlas::test_support::scheduler

#endif // ATLAS_TEST_SCHEDULER_TEST_SUPPORT_H
