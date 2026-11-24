#include "profiler_metrics/hot_loops.h"

#include <algorithm>

namespace Atlas
{
namespace
{
int32_t signExtend(uint32_t value, int bits)
{
    const uint32_t m = 1u << (bits - 1);
    return static_cast<int32_t>((value ^ m) - m);
}
} // namespace

std::optional<uint64_t> backwardTargetIndex(uint32_t word, uint64_t index)
{
    const uint32_t opcode = word & 0x7F;
    int32_t        offset = 0;
    if (opcode == 0x63) // B-type
    {
        const uint32_t imm = ((word >> 31) & 1) << 12 | ((word >> 7) & 1) << 11 |
                             ((word >> 25) & 0x3F) << 5 | ((word >> 8) & 0xF) << 1;
        offset = signExtend(imm, 13);
    }
    else if (opcode == 0x6F) // jal
    {
        const uint32_t imm = ((word >> 31) & 1) << 20 | ((word >> 12) & 0xFF) << 12 |
                             ((word >> 20) & 1) << 11 | ((word >> 21) & 0x3FF) << 1;
        offset = signExtend(imm, 21);
    }
    else
        return std::nullopt;
    if (offset > 0)
        return std::nullopt;
    const int64_t target = static_cast<int64_t>(index) + offset / 4;
    if (target < 0)
        return std::nullopt;
    return static_cast<uint64_t>(target);
}

std::map<int, uint64_t> lineCountsFrom(const std::map<uint64_t, uint64_t> &retiredByPc,
                                       const MetricsProgram &program)
{
    std::map<int, uint64_t> out;
    for (const auto &[pc, count] : retiredByPc)
    {
        const uint64_t index = pc / 4;
        if (index < program.line.size() && program.line[index] >= 0)
            out[program.line[index]] += count;
    }
    return out;
}

std::vector<HotLoop> findHotLoops(const MetricsProgram &program,
                                  const std::map<uint64_t, uint64_t> &retiredByPc, uint64_t retired,
                                  size_t top)
{
    const std::map<int, uint64_t> lines = lineCountsFrom(retiredByPc, program);
    const auto countAt = [&](uint64_t index)
    {
        const auto it = retiredByPc.find(index * 4);
        return it == retiredByPc.end() ? uint64_t{0} : it->second;
    };
    std::vector<HotLoop> loops;
    for (uint64_t i = 0; i < program.word.size(); ++i)
    {
        const std::optional<uint64_t> target = backwardTargetIndex(program.word[i], i);
        if (!target || *target >= program.word.size() || countAt(i) == 0)
            continue;
        HotLoop loop;
        loop.targetPc   = *target * 4;
        loop.branchPc   = i * 4;
        loop.firstLine  = program.line[*target];
        loop.lastLine   = program.line[i];
        loop.iterations = countAt(*target);
        for (auto it = lines.lower_bound(loop.firstLine); it != lines.end() && it->first <= loop.lastLine; ++it)
            loop.dynamic += it->second;
        loop.share = shareOf(loop.dynamic, retired);
        loops.push_back(loop);
    }
    std::sort(loops.begin(), loops.end(),
              [](const HotLoop &a, const HotLoop &b)
              {
                  if (a.dynamic != b.dynamic)
                      return a.dynamic > b.dynamic;
                  if (a.targetPc != b.targetPc)
                      return a.targetPc < b.targetPc;
                  return a.branchPc < b.branchPc;
              });
    if (loops.size() > top)
        loops.resize(top);
    return loops;
}
} // namespace Atlas
