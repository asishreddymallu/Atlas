/**
 * @file trace_json.h
 * @brief The ExecutionTrace's JSON form (WP-02), schema "atlas.trace/1".
 *
 *   {
 *     "schema": "atlas.trace/1",
 *     "model": "h_f", "hazard_detection": true, "forwarding": true,
 *     "single_cycle": false, "capacity": 100000, "dropped": 0,
 *     "cycles": [
 *       { "cycle": 3,
 *         "stages": { "IF": {...}, "ID": {...}, "EX": null, ... },   // null = bubble
 *         "stall": { "reason": "load_use", "continuing": false, "length": 1,
 *                    "producer": {...}, "producer_stage": "EX", "register": "x5" },  // or null
 *         "raw": [ { "register": "x5", "producer_stage": "EX", "producer": {...},
 *                    "producer_is_load": true, "resolution": "stall" } ],
 *         "forwarding": [ { "operand": "rs1", "register": "x5", "path": "MEM/WB",
 *                           "producer": {...}, "producer_is_load": true } ],
 *         "redirect": { "kind": "branch", "by": {...}, "squashed": [ {...} | null ] }  // or null
 *       } ] }
 *
 * An occupant is { "id", "pc", "instruction" (hex word), "line" (source
 * line, 1-based, or -1), "disassembly" (the assembled instruction) }. An RVSS file has
 * "single_cycle": true and only the stage "CI" per cycle.
 */
#pragma once

#include "common/assembled_program.h"
#include "processor/execution_trace.h"

#include <QJsonObject>
#include <QString>

#include <vector>

namespace Atlas
{
struct TraceMeta
{
    QString  model;
    bool     hazardDetection = false;
    bool     forwarding      = false;
    bool     singleCycle     = false;
    size_t   capacity        = ExecutionTrace::kDefaultCapacity;
    uint64_t dropped         = 0;
};

/// The 1-based source line of the instruction at pc, or -1.
int traceSourceLine(const AssembledProgram &program, uint64_t pc);
/// The assembled instruction at pc as text ("add x6, x5, x5"), or empty.
QString traceDisassembly(const AssembledProgram &program, uint64_t pc);

QJsonObject traceToJson(const std::vector<TraceCycle> &records, const TraceMeta &meta,
                        const AssembledProgram &program);

/// Reads back what traceToJson wrote: the structural fields (not "line" or
/// "disassembly", which come from the program). False on a schema mismatch.
bool traceFromJson(const QJsonObject &json, std::vector<TraceCycle> &records, TraceMeta &meta);
} // namespace Atlas
