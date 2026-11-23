/**
 * @file reorder_gate.cpp
 * @brief The measured safety gate for "Suggest reordering"; see reorder_gate.h.
 */
#include "scheduler_gate/reorder_gate.h"

#include "assembler/assembler.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "scheduler/basic_block_scheduler.h"
#include "scheduler/reorder_apply.h"

#include <array>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>

namespace Atlas
{
namespace
{
/// A five-stage model whose per-step undo deltas - where each store's
/// address and bytes are recorded - can be read. The deltas are protected;
/// a subclass exposes them read-only without touching the processor.
template <typename Vm> struct Observed : Vm
{
    using Vm::undo_stack_;
};

struct Run
{
    bool halted{false};
    unsigned int cycles{0};
    std::array<uint64_t, 32> x{};
    std::array<uint64_t, 32> f{};
    std::set<uint64_t> written;
    std::unique_ptr<ProcessorBase> vm;
};

template <typename Vm> Run runCapped(const AssembledProgram &program, unsigned int cap)
{
    auto vm = std::make_unique<Observed<Vm>>();
    vm->LoadProgram(program);
    // Stepped through ProcessorBase's public interface, as ProcessorManager
    // does; the five-stage base re-declares StepAndNotify() protected.
    ProcessorBase &processor = *vm;

    Run run;
    while (vm->cycle_s_ < cap)
    {
        // A read syscall would block on input no one will type here.
        if (processor.NextStepWaitsForInput())
            break;
        if (!processor.StepAndNotify())
        {
            run.halted = true;
            break;
        }
    }
    run.cycles = vm->cycle_s_;
    for (unsigned int i = 0; i < 32; ++i)
    {
        run.x[i] = vm->registers_.ReadGpr(i);
        run.f[i] = vm->registers_.ReadFpr(i);
    }
    for (std::stack<RV5StageStepDelta> deltas = vm->undo_stack_; !deltas.empty(); deltas.pop())
        for (const MemoryChange &change : deltas.top().memory_changes)
            for (uint64_t i = 0; i < change.new_bytes_vec.size(); ++i)
                run.written.insert(change.address + i);
    run.vm = std::move(vm);
    return run;
}

Run runOn(ProcessorType type, const AssembledProgram &program, unsigned int cap)
{
    return type == ProcessorType::RV5Stage_H_F ? runCapped<RV5StageProcessorHF>(program, cap)
                                               : runCapped<RV5StageProcessorHNF>(program, cap);
}

bool sameFinalState(Run &a, Run &b)
{
    if (a.x != b.x || a.f != b.f)
        return false;
    std::set<uint64_t> addresses = a.written;
    addresses.insert(b.written.begin(), b.written.end());
    for (uint64_t address : addresses)
        if (a.vm->memory_controller_.peekByte(address) != b.vm->memory_controller_.peekByte(address))
            return false;
    return true;
}

std::optional<AssembledProgram> tryAssemble(const std::string &text)
{
    try
    {
        std::istringstream in(text);
        return assemble(in);
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}
} // namespace

bool reorderingSupported(ProcessorType type)
{
    return type == ProcessorType::RV5Stage_H_F || type == ProcessorType::RV5Stage_H_NF;
}

ReorderSuggestion suggestReordering(const std::string &rawSource, ProcessorType type,
                                    unsigned int cycleCap)
{
    ReorderSuggestion out;
    if (!reorderingSupported(type))
    {
        out.status = ReorderStatus::UnsupportedProcessor;
        return out;
    }
    // Lines are moved in the text the user wrote, so it must be the text
    // that is assembled.
    if (customPseudoManager::expandPseudoInstruction(rawSource) != rawSource)
    {
        out.status = ReorderStatus::CustomPseudo;
        return out;
    }

    const std::optional<AssembledProgram> original = tryAssemble(rawSource);
    if (!original)
    {
        out.status = ReorderStatus::AssembleFailed;
        return out;
    }

    SchedulingOptions options;
    options.hazardDetectionEnabled = true;
    options.forwardingEnabled      = type == ProcessorType::RV5Stage_H_F;
    const SchedulingResult schedule = BasicBlockScheduler{}.schedule(*original, options);
    const ApplyResult applied       = applySchedule(rawSource, *original, schedule);
    if (!applied.changed)
    {
        out.status = ReorderStatus::NoChange;
        return out;
    }
    out.suggestedSource = applied.text;
    out.movedLines      = applied.movedLines;

    const std::optional<AssembledProgram> suggested = tryAssemble(applied.text);
    if (!suggested)
    {
        out.status = ReorderStatus::AssembleFailed;
        return out;
    }
    if (!sameInstructionMultiset(*original, *suggested))
    {
        out.status = ReorderStatus::MultisetDiffers;
        return out;
    }

    Run before = runOn(type, *original, cycleCap);
    Run after  = runOn(type, *suggested, cycleCap);
    out.originalCycles  = before.cycles;
    out.suggestedCycles = after.cycles;
    if (!before.halted || !after.halted)
        out.status = ReorderStatus::NotHalted;
    else if (!sameFinalState(before, after))
        out.status = ReorderStatus::StateDiffers;
    else if (after.cycles >= before.cycles)
        out.status = ReorderStatus::NotFaster;
    else
        out.status = ReorderStatus::Offered;
    return out;
}

std::string reorderStatusReason(ReorderStatus status)
{
    switch (status)
    {
    case ReorderStatus::Offered:
        return "The reordered program gives the same result in fewer cycles.";
    case ReorderStatus::UnsupportedProcessor:
        return "Suggestions are made only for the five-stage processors with hazard "
               "detection (with or without forwarding).";
    case ReorderStatus::CustomPseudo:
        return "The program uses custom pseudo-instructions, which reordering does not "
               "support.";
    case ReorderStatus::AssembleFailed:
        return "The program does not assemble.";
    case ReorderStatus::NoChange:
        return "No instruction line can be moved to save a cycle.";
    case ReorderStatus::NotHalted:
        return "A run did not finish within " + std::to_string(kReorderCycleCap) +
               " cycles, or waited for input.";
    case ReorderStatus::StateDiffers:
        return "The reordered program ended with different registers or memory.";
    case ReorderStatus::MultisetDiffers:
        return "The reordered program did not contain the same instructions.";
    case ReorderStatus::NotFaster:
        return "The reordered program was not faster when measured.";
    }
    return {};
}
} // namespace Atlas
