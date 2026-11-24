/**
 * @file retirement_stream.h
 * @brief What retired, cycle by cycle, read from the execution trace.
 *        Qt-free; it reads trace records and never touches a processor.
 *
 * Each TraceCycle names the instruction that retires in it: the occupant of
 * WB on a five-stage model, the single occupant on the single-cycle model.
 * Fed every record in cycle order, the stream keeps, for the whole run (not
 * only the trace's 100,000-cycle ring):
 *   - how many times each instruction address retired;
 *   - branches retired (B-type) and how many were taken: a branch is taken
 *     when the next instruction to retire is not at branch address + 4;
 *   - forwarded operands, by path (the EX/MEM or the MEM/WB register).
 *
 * Undo is followed exactly: each fed cycle's contribution is logged, and
 * rollbackTo() takes back the cycles after the one kept. A record that
 * arrives with a cycle already seen is treated as Undo-then-Redo.
 */
#pragma once

#include "processor/execution_trace.h"

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace Atlas
{
class RetirementStream
{
  public:
    void reset();

    /// Feeds the record of the next cycle. A cycle at or before the last one
    /// seen first rolls back to cycle - 1. A cycle more than one past the
    /// last one seen is a gap: the stream can no longer be complete.
    void feed(const TraceCycle &record);
    /// Keeps the cycles up to and including lastKept (Undo).
    void rollbackTo(uint32_t lastKept);
    /// Records that cycles were missed (the trace no longer held them).
    void markGap() { m_complete = false; }

    uint32_t lastCycle() const { return m_lastCycle; }
    /// False once a cycle was missed; every count is then incomplete.
    bool complete() const { return m_complete; }

    uint64_t retired() const { return m_retired; }
    /// Instruction address -> times retired.
    const std::map<uint64_t, uint64_t> &retiredByPc() const { return m_retiredByPc; }

    uint64_t branches() const { return m_branches; }
    /// Branches whose outcome is known (the next instruction has retired).
    uint64_t branchesResolved() const { return m_branchesResolved; }
    uint64_t branchesTaken() const { return m_taken; }

    uint64_t forwardsFromExMem() const { return m_fwdExMem; }
    uint64_t forwardsFromMemWb() const { return m_fwdMemWb; }

    /// The retiring occupant of a record, if one retired that cycle.
    static std::optional<TraceOccupant> retiring(const TraceCycle &record);
    static bool isBranch(uint32_t instruction) { return (instruction & 0x7F) == 0x63; }

  private:
    struct CycleLog
    {
        uint32_t cycle{0};
        uint64_t retiredPc{UINT64_MAX};   ///< UINT64_MAX: nothing retired
        uint64_t pendingBefore{UINT64_MAX}; ///< unresolved branch before this cycle
        bool     branch{false};           ///< the retiring instruction was a branch
        bool     resolved{false};         ///< this retire resolved the pending branch
        bool     taken{false};            ///< ... as taken
        uint16_t fwdExMem{0};
        uint16_t fwdMemWb{0};
    };

    std::vector<CycleLog>        m_log;
    std::map<uint64_t, uint64_t> m_retiredByPc;
    uint32_t m_lastCycle{0};
    bool     m_complete{true};
    uint64_t m_retired{0};
    uint64_t m_branches{0};
    uint64_t m_branchesResolved{0};
    uint64_t m_taken{0};
    uint64_t m_fwdExMem{0};
    uint64_t m_fwdMemWb{0};
    uint64_t m_pendingBranch{UINT64_MAX}; ///< address of the last branch, unresolved
};
} // namespace Atlas
