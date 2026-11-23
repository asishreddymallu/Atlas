/**
 * @file reorder_gate.h
 * @brief The safety gate in front of "Suggest reordering": a suggestion is
 *        offered only when it is MEASURED to be equivalent and faster.
 *
 * The scheduler (src/scheduler/, original author: Nandan Nihal,
 * CS25BTECH11014) proposes an order and the apply layer turns it into
 * source. Neither is trusted on its own. Before anything is shown, the
 * original and the suggested program are both run, headless, on the
 * selected processor model with a cycle cap, and the suggestion is offered
 * only if both halt, their final registers (x0-x31, f0-f31) and every byte
 * either of them wrote are identical, and the suggested run took strictly
 * fewer cycles. The cycles reported are those measured runs, never the
 * scheduler's own estimate.
 *
 * Only the five-stage models WITH hazard detection are supported: without
 * it a dependency can read a stale operand by design, so a reordering can
 * legitimately change the result, and the single-cycle model has no
 * stalls to remove.
 */
#pragma once

#include "processor/processor_types.h"

#include <cstddef>
#include <string>

namespace Atlas
{
enum class ReorderStatus
{
    Offered,              ///< Equivalent, and measured faster.
    UnsupportedProcessor, ///< RVSS or a five-stage model without hazard detection.
    CustomPseudo,         ///< The program uses custom pseudo-instructions.
    AssembleFailed,       ///< The program does not assemble.
    NoChange,             ///< Nothing can move at source-line level.
    NotHalted,            ///< A run hit the cycle cap or waited for input.
    StateDiffers,         ///< Final registers or memory differ.
    MultisetDiffers,      ///< The suggestion does not hold the same instructions.
    NotFaster,            ///< Measured cycles are not strictly lower.
};

struct ReorderSuggestion
{
    ReorderStatus status{ReorderStatus::NoChange};
    std::string suggestedSource;     ///< Set whenever a reordering was produced.
    unsigned int originalCycles{0};  ///< Measured; set once the runs happened.
    unsigned int suggestedCycles{0}; ///< Measured; set once the runs happened.
    size_t movedLines{0};

    [[nodiscard]] bool offered() const { return status == ReorderStatus::Offered; }
};

/// Cycles each of the two runs may take before the gate gives up.
inline constexpr unsigned int kReorderCycleCap = 50000;

/// True for RV5Stage_H_F and RV5Stage_H_NF.
[[nodiscard]] bool reorderingSupported(ProcessorType type);

/**
 * @param rawSource The editor's text, before custom pseudo expansion.
 * @param type      The processor model currently selected.
 */
[[nodiscard]] ReorderSuggestion suggestReordering(const std::string &rawSource, ProcessorType type,
                                                  unsigned int cycleCap = kReorderCycleCap);

/// One sentence for the user explaining a status.
[[nodiscard]] std::string reorderStatusReason(ReorderStatus status);
} // namespace Atlas
