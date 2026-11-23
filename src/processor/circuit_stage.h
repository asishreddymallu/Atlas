#pragma once

#include <QList>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <cstdint>

namespace Atlas
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

/// One cell of the circuit view's stage table: the instruction a pipeline
/// register holds, or for single-cycle the one just executed.
struct StageCell
{
    uint32_t instruction = 0x00000013; // NOP (processor_constants.h)
    uint64_t pc          = UINT64_MAX; // INVALID_PC
};

/// Everything the circuit view draws for one cycle, sent as one signal so
/// the wires, their stages and the table can never come from two cycles.
struct CircuitSnapshot
{
    QList<QString>   wires;  ///< active_wires_
    QList<quint8>    stages; ///< active_wire_stages_, same length
    QList<StageCell> cells;  ///< IF/ID, ID/EX, EX/MEM, MEM/WB - or the one CI cell
};
} // namespace Atlas

Q_DECLARE_METATYPE(Atlas::CircuitSnapshot)
