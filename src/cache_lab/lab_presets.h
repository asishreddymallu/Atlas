/**
 * @file lab_presets.h
 * @brief The cache setups the lab uses, in one table read by the Cache tab's
 *        "Lab setup" selector and by the tests. Qt-free.
 *
 * Exponents are the Cache tab's own fields: "2^n Lines", which sets the
 * number of SETS (known_issues 131), "2^n Ways" and "2^n words/line" (a
 * word is 4 bytes).
 */
#pragma once

#include "processor/cache/cacheconfig.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace Atlas
{
struct LabPreset
{
    std::string_view  name;
    int               setsExponent;  ///< the tab's "2^n Lines"
    int               waysExponent;  ///< "2^n Ways"
    int               wordsExponent; ///< "2^n words/line"
    WritePolicy       writeHit;
    AllocationPolicy  writeMiss;
    ReplacementPolicy replacement;
    std::string_view  description;   ///< one line, plain language

    [[nodiscard]] size_t sets() const { return size_t{1} << setsExponent; }
    [[nodiscard]] size_t ways() const { return size_t{1} << waysExponent; }
    [[nodiscard]] size_t lineBytes() const { return size_t{4} << wordsExponent; }
    [[nodiscard]] size_t totalBytes() const { return sets() * ways() * lineBytes(); }
    /// As the engine takes it: CacheConfig::lineCount is the set count.
    [[nodiscard]] CacheConfig config() const
    {
        return {sets(), lineBytes(), ways(), writeHit, writeMiss, replacement};
    }
};

/// Every preset, in the order the selector lists them.
[[nodiscard]] const std::vector<LabPreset> &labPresets();

/// The preset called `name`, or nullptr.
[[nodiscard]] const LabPreset *findLabPreset(std::string_view name);

/// The step delay a preset sets, in milliseconds.
inline constexpr unsigned int kLabPresetStepDelayMs = 1;
} // namespace Atlas
