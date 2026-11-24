/**
 * @file instruction_mix.h
 * @brief Metric 1: the retired-instruction mix. Qt-free.
 *
 * Counts are of machine instructions as they retire (RetirementStream), so
 * a pseudo-instruction that expands to two instructions counts as those
 * two. Counts are Measured; shares are Modelled (a count over the retired
 * total) and absent when nothing has retired.
 */
#pragma once

#include "common/assembled_program.h"
#include "common/instruction_types.h"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace Atlas
{
/// What the metrics need to know about the loaded program, per instruction
/// index (address / 4).
struct MetricsProgram
{
    std::vector<std::string> mnemonic; ///< the machine instruction's mnemonic
    std::vector<int>         line;     ///< its source line, -1 if unknown
    std::vector<uint32_t>    word;     ///< its encoding

    static MetricsProgram from(const AssembledProgram &program);
    /// The mnemonic at an instruction address, "" outside the program.
    std::string mnemonicAt(uint64_t pc) const;
};

struct MnemonicCount
{
    std::string mnemonic;
    uint64_t    count{0};
};

struct InstructionMix
{
    uint64_t retired{0};
    std::array<uint64_t, instruction_set::kInstructionTypeCount> byType{};
    uint64_t loads{0};
    uint64_t stores{0};
    /// Sorted by count (largest first), then by name.
    std::vector<MnemonicCount> byMnemonic;
};

bool isLoadMnemonic(const std::string &mnemonic);
bool isStoreMnemonic(const std::string &mnemonic);

InstructionMix computeInstructionMix(const std::map<uint64_t, uint64_t> &retiredByPc,
                                     const MetricsProgram &program);

/// part / whole, or nothing when whole is 0.
std::optional<double> shareOf(uint64_t part, uint64_t whole);
} // namespace Atlas
