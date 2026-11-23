/**
 * @file rv5s_control_unit.h
 * @brief Header for the 5-stage pipelined Control Unit.
 * @author Atharva and Harshit
 */
#ifndef RV5S_CONTROL_UNIT_H
#define RV5S_CONTROL_UNIT_H

#include "processor/alu.h"
#include "processor/control_unit_base.h"

namespace Atlas
{
class RV5SControlUnit : public ControlUnit
{
  public:
    void SetControlSignals(uint32_t instruction) override;
    alu::AluOp GetAluSignal(uint32_t instruction, bool ALUOp) override;

    /**@brief The ALU row for `instruction` under ALUOp row `alu_op`.
     *
     * The pipeline calls this and passes `id_ex_reg_.alu_op`, the row
     * latched at decode, instead of letting the Execute stage read the
     * member `alu_op_` the previous cycle's decode left behind. The
     * member is not part of the step delta, so an Undo could not restore
     * it: known_issues 82, and the comment on the definition.
     */
    alu::AluOp GetAluSignalFor(uint32_t instruction, uint8_t alu_op);
};
}//namespace Atlas
#endif // RV5S_CONTROL_UNIT_H