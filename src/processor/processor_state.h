#pragma once
#include "processor/processor_types.h"
#include "utils/to_index.h"
#include <vector>
#include <cstdint>
#include <string>
#include <vector>
namespace Kites
{
// enum class PipelineStage
// {
//     IF,
//     ID,
//     EX,
//     MEM,
//     WB,
//     PipelineStageCount
// };

// inline std::string pipelineStageToString(PipelineStage stage)
// {
//     switch (stage)
//     {
//     case PipelineStage::IF:
//         return "IF";
//     case PipelineStage::ID:
//         return "ID";
//     case PipelineStage::EX:
//         return "EX";
//     case PipelineStage::MEM:
//         return "MEM";
//     case PipelineStage::WB:
//         return "WB";
//     default:
//         return "";
//     }
// }
// struct PipelineData
// {
//     std::array<std::string, toIndex(PipelineStage::PipelineStageCount)> instructionTexts{};
//     std::array<uint64_t, toIndex(PipelineStage::PipelineStageCount)> sourceLineNumbers{};
//     std::array<uint64_t, toIndex(PipelineStage::PipelineStageCount)> disassemblyLineNumbers{};
// };

// struct ProcessorStatistics
// {
//     uint64_t cycleCount{};
//     uint64_t instructionsRetiredCount{};
//     uint64_t branchMispredictionCount{};
//     uint64_t stallCycleCount{};
//     double cpi{};
//     double ipc{};
// };

// struct SingleStageProcessorState
// {
//     uint64_t programCounter{};
// };

// struct FiveStageProcessorState
// {
//     std::array<uint64_t, toIndex(PipelineStage::PipelineStageCount)> programCounters{};
//     bool isStalled{};
// };

// using ProcessorState = std::variant<SingleStageProcessorState, FiveStageProcessorState>;

struct ProcessorState
{
    std::vector<uint64_t> programCounters;
    uint64_t lastExecutedPC{UINT64_MAX};
    /// The instruction that retired on the clock this state was emitted
    /// for (UINT64_MAX, i.e. INVALID_PC, for none). Only meaningful on a
    /// clock that increased instructionsRetired; Undo and Redo leave it
    /// alone, and the Profiler replays its own history for those.
    uint64_t retiredPC{UINT64_MAX};
    /// The processor's instructions_retired_ when this state was emitted.
    /// The Profiler counts by its changes: +1 is a retire, a drop is an
    /// Undo, no change is a refresh or a bubble.
    uint64_t instructionsRetired{0};
    void reset()
    {
        programCounters.assign(programCounters.size(), 0);
    }
    //for further extension we can add more fields here
    // tho beware if this struct is too big we lose on performance
};

} // namespace Kites
