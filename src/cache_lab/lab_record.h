/**
 * @file lab_record.h
 * @brief One JSON experiment record per run, schema "atlas.lab-record/1".
 *        Qt-free: plain data in, a JSON string out.
 *
 * The Cache tab's "Export lab record..." fills a LabRecordInput from the
 * window and its caches (lab_record_capture.h) and writes what
 * buildLabRecordJson() returns. The schema is documented in
 * docs/cache_lab.md.
 */
#pragma once

#include "common/version.h" // atlasVersion(), which the record carries

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Atlas
{
inline constexpr std::string_view kLabRecordSchema = "atlas.lab-record/1";

struct LabRecordCache
{
    std::string level; ///< "instruction", "l1" or "l2"

    // Geometry: the Cache tab's exponents and what they come to.
    int    setsExponent{0};  ///< the tab's "2^n Lines" (it sets the number of sets)
    int    waysExponent{0};  ///< "2^n Ways"
    int    wordsExponent{0}; ///< "2^n words/line"
    size_t sets{0}, ways{0}, lineBytes{0}, totalBytes{0};

    // Policies, as the tab names them.
    std::string writeHit;    ///< "write-through" or "write-back"
    std::string writeMiss;   ///< "write-allocate" or "no-write-allocate"
    std::string replacement; ///< "lru", "fifo" or "custom"

    // Results.
    uint64_t hits{0}, misses{0}, evictions{0}, writeBacks{0};
};

struct LabRecordInput
{
    std::string                atlasVersion;
    std::string                timestamp;     ///< ISO 8601, UTC
    std::string                programName;
    std::string                programSha256; ///< lower-case hex of the source's SHA-256
    std::string                processor;
    std::optional<std::string> preset;        ///< a lab preset's name, if one was in use
    std::vector<LabRecordCache> caches;
    uint64_t                   cycles{0};
    uint64_t                   instructionsRetired{0};
    bool                       completed{false}; ///< false if the run was stopped early
};

/// The record as a JSON document (pretty-printed, newline-terminated).
[[nodiscard]] std::string buildLabRecordJson(const LabRecordInput &input);
} // namespace Atlas
