/**
 * @file branch_stats.h
 * @brief Metric 2: branch statistics. Qt-free.
 *
 * Branches retired are B-type instructions (beq bne blt bge bltu bgeu). A
 * branch is taken when the next instruction to retire is not at the branch
 * address + 4 (RetirementStream). Counts are Measured; the rates are
 * Modelled. The misprediction rate divides the model's own misprediction
 * counter by the branches retired, so it exists only on the models that
 * keep that counter (the five-stage pipelines).
 */
#pragma once

#include <cstdint>
#include <optional>

namespace Atlas
{
struct BranchStats
{
    uint64_t branches{0};
    uint64_t taken{0};
    std::optional<double> takenRate;         ///< taken / branches
    std::optional<double> mispredictionRate; ///< mispredictions / branches
};

BranchStats computeBranchStats(uint64_t branches, uint64_t taken,
                               std::optional<uint64_t> mispredictions);
} // namespace Atlas
