#include "profiler/profiler.h"
#include "common/instructions.h"

namespace Kites
{

namespace instr = instruction_set;

Profiler::Profiler(QObject *parent) : QObject(parent)
{
}

void Profiler::Reset()
{
    m_lineNumberToExecutionCounts.clear();
    m_lineNumberToinstructionType.clear();
    m_instructionNumberToLineNumber.clear();
    m_instructionTypeCounts.fill(0);
    m_retiredLines.clear();
    m_undoneLines.clear();
    emit profilerReset();
    emit updateInstructionTypeCountsSignal();
}

void Profiler::setInstructionToLineMapping(const AssembledProgram &program)
{
    m_instructionNumberToLineNumber = program.instruction_number_line_number_mapping;
}

// void Profiler::setInstructionTypeCounts(const AssembledProgram &program)
// {
//     m_instructionTypeCounts.fill(0);
//     for (const auto &[icUnit, _] : program.intermediate_code)
//     {
//         const auto mnemonic = icUnit.getOpcode();
//         if (!mnemonic.empty())
//         {
//             instr::InstructionType instrType = instr::getInstructionType(mnemonic);
//             m_instructionTypeCounts[toIndex(instrType)]++;
//         }
//     }
//     emit updateInstructionTypeCountsSignal(m_instructionTypeCounts);
// }

const InstructionTypeCounts &Profiler::getInstructionTypeCounts() const
{
    return m_instructionTypeCounts;
}

int Profiler::getLineExecutionCount(int lineNumber) const
{
    const auto it = m_lineNumberToExecutionCounts.find(lineNumber);
    if (it != m_lineNumberToExecutionCounts.end())
    {
        return it->second;
    }
    return 0;
}

void Profiler::setLineNumberToInstructionTypeMapping(const AssembledProgram &program)
{
    m_lineNumberToinstructionType.clear();
    // A load replaces the program, so its counts replace the last one's
    // (they used to accumulate: known_issues 85).
    m_instructionTypeCounts.fill(0);
    for (const auto &[icUnit, _] : program.intermediate_code)
    {
        const auto lineNumber = icUnit.getLineNumber();
        const auto mnemonic = icUnit.getOpcode();
        if (!mnemonic.empty())
        {
            instr::InstructionType instrType = instr::getInstructionType(mnemonic);
            m_lineNumberToinstructionType[lineNumber] = instrType;
            // Checked, not trusted: every enumerator is inside the count
            // now, but an out-of-range value must land on "Other" rather
            // than past the array (the Run abort this replaced).
            const size_t index = toIndex(instrType);
            m_instructionTypeCounts[index < m_instructionTypeCounts.size()
                                        ? index
                                        : toIndex(instr::InstructionType::UNKNOWN)]++;
        }
    }
    emit updateLineInstructionTypeSignal(m_lineNumberToinstructionType);
    emit updateInstructionTypeCountsSignal();
}

int Profiler::lineOfPc(uint64_t pc) const
{
    // find, not operator[]: a bubble's INVALID_PC used to be inserted and
    // counted as line 0 (known_issues 85).
    const auto it = m_instructionNumberToLineNumber.find(static_cast<unsigned int>(pc / 4));
    return it == m_instructionNumberToLineNumber.end() ? -1 : static_cast<int>(it->second);
}

void Profiler::addToLine(int lineNumber, int delta)
{
    if (lineNumber < 0)
        return;
    int &count = m_lineNumberToExecutionCounts[lineNumber];
    count += delta;
    const int now = count;
    if (now <= 0)
        m_lineNumberToExecutionCounts.erase(lineNumber);
    emit lineExecutionCountChangedSignal(lineNumber, now > 0 ? now : 0);
}

void Profiler::processorClockedSlot(const ProcessorState &processorState)
{
    // Counted by retirement, from the change in the processor's retired
    // count, not by signal: Undo, Redo and the extra clock a Run emits on
    // Stop all emit this signal too, and used to add a count each.
    const size_t retired = static_cast<size_t>(processorState.instructionsRetired);
    const size_t seen    = m_retiredLines.size();

    if (retired < seen)
    {
        // Undo: take back what the undone cycles retired.
        while (m_retiredLines.size() > retired)
        {
            const int line = m_retiredLines.back();
            m_retiredLines.pop_back();
            m_undoneLines.push_back(line);
            addToLine(line, -1);
        }
        return;
    }
    if (retired != seen + 1)
        return; // nothing retired (a bubble, a stall, a refresh), or out of step

    int line = -1;
    if (m_replayingRedo && !m_undoneLines.empty())
    {
        line = m_undoneLines.back();
        m_undoneLines.pop_back();
    }
    else
    {
        // A fresh cycle: what was undone is gone, as in the processor.
        m_undoneLines.clear();
        line = lineOfPc(processorState.retiredPC);
    }
    m_retiredLines.push_back(line);
    addToLine(line, +1);
}

// uint64_t Profiler::resolveExecutedLineFromState(const ProcessorState& processorState) const
// {
//     if (processorState.programCounters.empty())
//     {
//         return -1; // No program counters available
//     }

//     uint64_t programCounter = processorState.programCounters[0];
//     unsigned int instructionNumber = static_cast<unsigned int>(programCounter / 4);

//     const auto it = m_instructionNumberToLineNumber.find(instructionNumber);
//     if (it != m_instructionNumberToLineNumber.end())
//     {
//         return static_cast<int>(it->second);
//     }
//     return -1; // Not found
// }

instr::InstructionType Profiler::getInstructionTypeForLine(int lineNumber) const
{
    const auto it = m_lineNumberToinstructionType.find(lineNumber);
    if (it != m_lineNumberToinstructionType.end())
    {
        return it->second;
    }
    return instr::InstructionType::UNKNOWN;
}
}//namespace Kites
