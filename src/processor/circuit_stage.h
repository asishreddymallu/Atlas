#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace Kites
{
/**
 * @brief The pipeline stage a lit wire belongs to.
 *
 * After a Step each stage's instruction is the one its label shows
 * (setProcessorState): ID = if_id_reg_, EX = id_ex_reg_, MEM = ex_mem_reg_,
 * WB = mem_wb_reg_, IF = program_counter_. A conditional wire is tagged
 * with the stage whose instruction it is drawn for (known_issues 95's
 * audit); the always-on backbone carries no stage.
 */
enum class WireStage : quint8
{
    IF = 0,
    ID,
    EX,
    MEM,
    WB
};

/// One bit per stage, so a wire lit for two stages carries both.
constexpr quint8 stageBit(WireStage stage)
{
    return static_cast<quint8>(1u << static_cast<quint8>(stage));
}

/// The bitmask of a wire with no stage: backbone, or any RVSS wire.
constexpr quint8 kNoStage = 0;
} // namespace Kites
