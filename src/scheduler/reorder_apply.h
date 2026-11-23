/**
 * @file reorder_apply.h
 * @brief Turns a SchedulingResult into reordered SOURCE TEXT, moving whole
 *        source lines. Qt-free, part of atlas_scheduler.
 *
 * The scheduler (basic_block_scheduler.h, original author: Nandan Nihal,
 * CS25BTECH11014) reorders assembled instructions. A suggestion has to be
 * source the user can read and keep, so this layer maps instructions back
 * to the lines they came from (ICUnit::line_number) and moves lines:
 *
 *  - Only a line that assembles to EXACTLY ONE instruction, and defines no
 *    label, can move.
 *  - Every other line stays where it is and is a barrier nothing moves
 *    across: labels, directives, comment-only lines, and lines that expand
 *    to several instructions (la, call, li with a large immediate, ...).
 *  - Blank lines stay where they are but are not barriers.
 *
 * Within each scheduler block, a maximal run of movable lines (a segment)
 * is rewritten in the scheduled order restricted to that segment. The
 * scheduled order is a topological order of the block's dependency graph,
 * so its restriction to any subset keeps every edge between members, and a
 * segment's lines only trade places with each other, so nothing moves
 * relative to anything outside it: the result is always legal, even where
 * it cannot realise the whole scheduled order.
 *
 * Callers re-assemble the new text and check sameInstructionMultiset().
 */
#pragma once

#include "common/assembled_program.h"
#include "scheduler/basic_block_scheduler.h"

#include <cstddef>
#include <string>

namespace Atlas
{
struct ApplyResult
{
    std::string text;      ///< The new source. Equal to the input when nothing moved.
    bool changed{false};   ///< Any line moved.
    size_t movedLines{0};  ///< Lines whose content changed position.
};

/**
 * @param source  The exact text `program` was assembled from (line N of the
 *                text is ICUnit::line_number N).
 * @param program The assembled program the schedule was computed for.
 * @param result  BasicBlockScheduler::schedule(program, ...).
 */
[[nodiscard]] ApplyResult applySchedule(const std::string &source, const AssembledProgram &program,
                                        const SchedulingResult &result);

/// True when both programs contain the same instructions (opcode and every
/// operand, ignoring position), each the same number of times.
[[nodiscard]] bool sameInstructionMultiset(const AssembledProgram &a, const AssembledProgram &b);
} // namespace Atlas
