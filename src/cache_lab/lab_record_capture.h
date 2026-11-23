/**
 * @file lab_record_capture.h
 * @brief Fills the plain-data LabRecordCache from a live Cache. The bridge
 *        between the engine and the Qt-free record builder.
 */
#pragma once

#include "cache_lab/lab_record.h"
#include "processor/cache/cache.h"

#include <string>

namespace Atlas
{
/// `level` is "instruction", "l1" or "l2".
[[nodiscard]] LabRecordCache captureLabRecordCache(const std::string &level, const Cache &cache);

/// Lower-case hex SHA-256 of `source` (Qt's QCryptographicHash).
[[nodiscard]] std::string sha256Hex(const std::string &source);

/// Now, as ISO 8601 UTC ("2026-11-24T10:15:30Z").
[[nodiscard]] std::string utcTimestamp();
} // namespace Atlas
