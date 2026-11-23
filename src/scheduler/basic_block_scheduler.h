/**
 * @file basic_block_scheduler.h
 * @brief Conservative Phase-1 instruction scheduler for Atlas basic blocks.
 *
 * Original author: Nandan Nihal (CS25BTECH11014)
 *
 * The scheduler has no Qt dependency.  It consumes the assembler's intermediate
 * code and returns an alternative order only when all register and memory
 * ordering constraints are preserved.  Applying the result to source code is a
 * later UI concern; this module deliberately does not modify a program.
 */
#pragma once

#include "common/assembled_program.h"
#include <cstddef>
#include <vector>

namespace Atlas
{
enum class DependencyKind
{
    ReadAfterWrite,
    WriteAfterRead,
    WriteAfterWrite,
    MemoryOrder,
    Barrier
};

struct DependencyEdge
{
    size_t producer{}; ///< Index within BasicBlockSchedule::originalInstructionIndices.
    size_t consumer{}; ///< Index within BasicBlockSchedule::originalInstructionIndices.
    DependencyKind kind{};
    unsigned int latency{}; ///< Earliest cycle distance required by the selected pipeline mode.
};

/** Pipeline assumptions used for scheduling and cycle estimates. */
struct SchedulingOptions
{
    bool forwardingEnabled{true};
    bool hazardDetectionEnabled{true};
};

struct BasicBlockSchedule
{
    std::vector<size_t> originalInstructionIndices;
    std::vector<size_t> scheduledInstructionIndices;
    std::vector<DependencyEdge> dependencies;
    unsigned int originalCycleCount{};
    unsigned int scheduledCycleCount{};
};

struct SchedulingResult
{
    std::vector<BasicBlockSchedule> blocks;
    unsigned int originalCycleCount{};
    unsigned int scheduledCycleCount{};

    [[nodiscard]] bool changed() const { return scheduledCycleCount < originalCycleCount; }
};

/**
 * Splits an assembled program into basic blocks, builds conservative dependency
 * graphs, and list-schedules each block.  Phase 1 supports the integer I/M
 * instruction subset; unsupported instructions are treated as ordering barriers.
 */
class BasicBlockScheduler
{
  public:
    [[nodiscard]] SchedulingResult schedule(const AssembledProgram &program,
                                             const SchedulingOptions &options) const;
};
} // namespace Atlas
