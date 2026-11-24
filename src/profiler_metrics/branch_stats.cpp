#include "profiler_metrics/branch_stats.h"

#include "profiler_metrics/instruction_mix.h"

namespace Atlas
{
BranchStats computeBranchStats(uint64_t branches, uint64_t taken,
                               std::optional<uint64_t> mispredictions)
{
    BranchStats s;
    s.branches  = branches;
    s.taken     = taken;
    s.takenRate = shareOf(taken, branches);
    if (mispredictions)
        s.mispredictionRate = shareOf(*mispredictions, branches);
    return s;
}
} // namespace Atlas
