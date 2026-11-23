/**
 * @file trace_explain.h
 * @brief WP-03's hazard explanation panel: one plain-language sentence for
 *        one instruction in one cycle, generated from the trace's structured
 *        record and nothing else.
 *
 * Every claim comes from the record: a stall is mentioned only when the
 * record says the machine stalled, and its cost is the length of the stall
 * run the trace recorded, never a number assumed from the model. On the
 * processors without hazard detection the sentence says what the program
 * must do instead (pad with nops).
 */
#pragma once

#include "processor/execution_trace.h"

#include <functional>
#include <string>
#include <vector>

namespace Atlas
{
struct ExplainModel
{
    bool hazardDetection = false;
    bool forwarding      = false;
};

/// "(hazard detection + forwarding)" and the like.
std::string modelPhrase(const ExplainModel &model);

/// The sentence for the instance `id` in record `index` of `records`
/// (consecutive cycles). `label` names an instruction by its PC.
std::string explainTraceCell(const std::vector<TraceCycle> &records, size_t index, uint32_t id,
                             const ExplainModel &model, const std::function<std::string(uint64_t)> &label);
} // namespace Atlas
