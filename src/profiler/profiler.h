#pragma once

#include <QMap>
#include <QObject>
#include <QVariant>
#include <map>
#include <string>
#include <vector>
#include "common/assembled_program.h"
#include "common/instruction_types.h"
#include "utils/to_index.h"
#include "processor/processor_state.h"

namespace Kites
{

using InstructionTypeCounts = std::array<size_t, 
                            toIndex(instruction_set::InstructionType::INSTRUCTION_TYPE_COUNT)>;
                            
class Profiler : public QObject
{
    Q_OBJECT
  
public:
    explicit Profiler(QObject *parent = nullptr);

    void Reset();
    void setInstructionToLineMapping(const AssembledProgram &program);
     
    void setLineNumberToInstructionTypeMapping(const AssembledProgram &program);
    //void setInstructionTypeCounts(const AssembledProgram &program);

    const InstructionTypeCounts& getInstructionTypeCounts() const;

    int getLineExecutionCount(int lineNumber)const;

    /// Every line that has retired at least once -> times retired.
    const std::map<int, int> &getLineExecutionCounts() const { return m_lineNumberToExecutionCounts; }

    /// Set by ProcessorManager::redo around the processor's Redo(): the
    /// state Redo emits restores a saved cycle and carries no fresh
    /// retiredPC, so a retire seen while this is set is replayed from
    /// what Undo took back.
    void setReplayingRedo(bool replaying) { m_replayingRedo = replaying; }

public slots:
    void processorClockedSlot(const ProcessorState& processorState);

public:
    // Get instruction type for a specific line
    // uint64_t resolveExecutedLineFromState(const ProcessorState& processorState) const;
    instruction_set::InstructionType getInstructionTypeForLine(int lineNumber) const;
    // Get all instruction types mapped by line number
    const std::map<int, instruction_set::InstructionType> getLineInstructionTypes() const
    {
      return m_lineNumberToinstructionType;
    }

private:
   
    std::map<unsigned int, unsigned int> m_instructionNumberToLineNumber{};       
    std::map<int, int>                   m_lineNumberToExecutionCounts{}; 
    std::map<int, instruction_set::InstructionType>       m_lineNumberToinstructionType{};

    InstructionTypeCounts m_instructionTypeCounts{};

    // The line of every instruction retired so far, oldest first. Its
    // size is always the processor's instructions_retired_ as last seen.
    std::vector<int> m_retiredLines{};
    // What Undo took off m_retiredLines, for Redo to put back.
    std::vector<int> m_undoneLines{};
    bool             m_replayingRedo{false};

    int  lineOfPc(uint64_t pc) const;
    void addToLine(int lineNumber, int delta);
signals:
    void profilerReset();
    /// A line's count changed (up on a retire, down on an Undo).
    void lineExecutionCountChangedSignal(int lineNumber, int count);
    void updateLineInstructionTypeSignal(std::map<int, instruction_set::InstructionType> lineInstructionTypes = {});
    void updateInstructionTypeCountsSignal();
};
}//namespace Kites
