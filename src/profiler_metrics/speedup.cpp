#include "profiler_metrics/speedup.h"

namespace Atlas
{
std::optional<double> speedupOver(std::optional<unsigned> rvssCycles,
                                  std::optional<unsigned> modelCycles)
{
    if (!rvssCycles || !modelCycles || *rvssCycles == 0 || *modelCycles == 0)
        return std::nullopt;
    return static_cast<double>(*rvssCycles) / static_cast<double>(*modelCycles);
}
} // namespace Atlas
