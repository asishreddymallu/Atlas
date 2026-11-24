/**
 * @file hot_loops.h
 * @brief Metric 5: the hottest loops. Qt-free.
 *
 * A loop is a retired-at-least-once backward branch or jal: its target is at
 * or before it (jalr targets are only known at run time and are not
 * considered). Its body is the source lines from the target's line to the
 * branch's line. Iterations = how many times the target instruction
 * executed; dynamic instructions = the per-line execution counts summed over
 * the body; share = dynamic / retired. Loops are ranked by dynamic
 * instructions: instructions executed, not cycles (Atlas keeps no per-line
 * cycle counts). Nested loops each appear; an outer loop's body includes its
 * inner loops.
 */
#pragma once

#include "profiler_metrics/instruction_mix.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace Atlas
{
struct HotLoop
{
    uint64_t targetPc{0};
    uint64_t branchPc{0};
    int      firstLine{-1};
    int      lastLine{-1};
    uint64_t iterations{0};
    uint64_t dynamic{0};
    std::optional<double> share;
};

/// The backward branch or jal target of the instruction at `index`, or
/// nothing (not a branch/jal, or a forward one).
std::optional<uint64_t> backwardTargetIndex(uint32_t word, uint64_t index);

/// Per-line execution counts from per-address retire counts.
std::map<int, uint64_t> lineCountsFrom(const std::map<uint64_t, uint64_t> &retiredByPc,
                                       const MetricsProgram &program);

/// The `top` loops with the most dynamic instructions (ties: earlier target,
/// then earlier branch).
std::vector<HotLoop> findHotLoops(const MetricsProgram &program,
                                  const std::map<uint64_t, uint64_t> &retiredByPc,
                                  uint64_t retired, size_t top = 3);
} // namespace Atlas
