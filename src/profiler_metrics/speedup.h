/**
 * @file speedup.h
 * @brief Metric 3: speed-up over the single-cycle processor. Qt-free.
 *
 * Speed-up = the program's cycles on the single-cycle processor (rvss) /
 * its cycles on the selected model. Modelled. It compares cycle counts only:
 * a pipeline's advantage is a shorter clock period, which Atlas does not
 * model.
 */
#pragma once

#include <optional>

namespace Atlas
{
inline constexpr const char *kSpeedupCaveat =
    "cycle counts only; a pipeline's advantage is a shorter clock period, which Atlas does not "
    "model";

/// rvss cycles / model cycles; nothing when either is unknown or 0.
std::optional<double> speedupOver(std::optional<unsigned> rvssCycles,
                                  std::optional<unsigned> modelCycles);
} // namespace Atlas
