/**
 * @file execution_trace.h
 * @brief WP-02: a per-cycle record of what the pipeline did, kept apart
 *        from the UI.
 *
 * One TraceCycle per clock cycle (one Step). It is built from the machine's
 * own registers before and after the step, by the step-delta hooks every
 * model already calls. It reads state and never writes it, so it cannot
 * change a value or a cycle count.
 *
 * Occupancy is the textbook one. During cycle N the instruction in each
 * stage is the one in the register in front of it before the step:
 *     IF = the PC,  ID = IF/ID,  EX = ID/EX,  MEM = EX/MEM,  WB = MEM/WB.
 * Step() runs WB first and IF last, so these are the instructions each stage
 * works on in that cycle.
 *
 * Squashes are recorded from what happened: an instruction that was in a
 * stage before the step and is in neither the next stage nor (when stalled
 * or held) the same one afterwards was flushed. A stall is recorded only
 * when the machine's own stall counter (total_stall_cycles_) grew.
 *
 * RVSS (single cycle) records one occupant per cycle, in occ[0]; the other
 * four slots stay empty and singleCycle is true.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

namespace Atlas
{
enum class TraceStage : uint8_t
{
    IF = 0,
    ID,
    EX,
    MEM,
    WB
};
constexpr size_t kTraceStages = 5;
const char *traceStageName(TraceStage stage);

/// One instruction in one stage, or a bubble (id == 0).
struct TraceOccupant
{
    uint64_t pc          = UINT64_MAX;
    uint32_t instruction = 0x00000013;
    uint32_t id          = 0; ///< instance id: one per fetch, 0 = bubble
    bool     bubble() const { return id == 0; }
    bool     operator==(const TraceOccupant &) const = default;
};

enum class RegFile : uint8_t
{
    X,
    F
};

enum class StallReason : uint8_t
{
    None = 0,
    LoadUse,         ///< forwarding on: a load in EX, its consumer in ID
    RawNoForwarding, ///< forwarding off: a producer in EX or MEM, its consumer in ID
    Unknown          ///< the machine stalled and no dependence explains it
};
const char *stallReasonName(StallReason reason);

struct TraceStall
{
    StallReason reason        = StallReason::None;
    bool        continuing    = false; ///< second cycle of a two-cycle stall
    uint8_t     length        = 0;     ///< cycles the hazard unit decided on (1 or 2)
    TraceStage  producerStage = TraceStage::EX;
    uint32_t    producerId    = 0;
    uint64_t    producerPc    = UINT64_MAX;
    uint32_t    producerInstruction = 0x00000013;
    RegFile     file          = RegFile::X;
    uint8_t     reg           = 0;
    bool        operator==(const TraceStall &) const = default;
};

enum class RawResolution : uint8_t
{
    Stall,    ///< the hazard unit holds the consumer in ID
    Forward,  ///< a forwarding path will deliver the value in EX
    Unhandled ///< no hazard unit: the consumer reads a stale value
};
const char *rawResolutionName(RawResolution resolution);

/// A RAW dependence of the instruction in ID on one still in flight.
struct TraceRaw
{
    RegFile       file          = RegFile::X;
    uint8_t       reg           = 0;
    TraceStage    producerStage = TraceStage::EX; ///< EX (distance 1) or MEM (distance 2)
    uint32_t      producerId    = 0;
    uint64_t      producerPc    = UINT64_MAX;
    uint32_t      producerInstruction = 0x00000013;
    bool          producerIsLoad = false;
    RawResolution resolution    = RawResolution::Unhandled;
    bool          operator==(const TraceRaw &) const = default;
};

/// A forwarding path into the instruction in EX.
struct TraceForward
{
    uint8_t    operand  = 1; ///< 1 = rs1, 2 = rs2, 3 = rs3
    RegFile    file     = RegFile::X;
    uint8_t    reg      = 0;
    TraceStage from     = TraceStage::MEM; ///< MEM = the EX/MEM path, WB = the MEM/WB path
    uint32_t   producerId = 0;
    uint64_t   producerPc = UINT64_MAX;
    bool       producerIsLoad = false;
    bool       operator==(const TraceForward &) const = default;
};

enum class RedirectKind : uint8_t
{
    None = 0,
    Branch, ///< taken B-type, resolved in MEM
    Jump,   ///< jal/jalr, resolved in EX
    Csr,    ///< CSR instruction, served at WB
    Ecall   ///< ecall, served at WB
};
const char *redirectKindName(RedirectKind kind);

struct TraceRedirect
{
    RedirectKind               kind = RedirectKind::None;
    uint32_t                   byId = 0;
    uint64_t                   byPc = UINT64_MAX;
    std::vector<TraceOccupant> squashed; ///< youngest first: IF, ID, EX, MEM
    bool                       operator==(const TraceRedirect &) const = default;
};

struct TraceCycle
{
    uint32_t                               cycle       = 0; ///< 1-based: the Nth Step
    bool                                   singleCycle = false;
    std::array<TraceOccupant, kTraceStages> occ{};
    bool                                   stalled = false;
    TraceStall                             stall{};
    std::vector<TraceRaw>                  raw;
    std::vector<TraceForward>              fwd;
    TraceRedirect                          redirect{};
    /// Instance ids after the step, and the id counter before/after it:
    /// what Undo restores and Redo re-applies.
    std::array<uint32_t, kTraceStages>     postIds{};
    uint32_t                               nextIdBefore = 1;
    uint32_t                               nextIdAfter  = 1;
    bool                                   operator==(const TraceCycle &) const = default;
};

/**
 * @brief A ring buffer of TraceCycles, safe to read from the UI thread
 *        while the worker writes.
 */
class ExecutionTrace
{
  public:
    static constexpr size_t kDefaultCapacity = 100000;
    explicit ExecutionTrace(size_t capacity = kDefaultCapacity) : m_capacity(capacity ? capacity : 1) {}

    void push(const TraceCycle &record);
    /// Drops every record with cycle > lastKept (Undo).
    void truncateAfter(uint32_t lastKept);
    void clear();

    size_t   capacity() const { return m_capacity; }
    size_t   size() const;
    /// Records dropped off the front because the buffer was full.
    uint64_t dropped() const;
    /// The cycle numbers held, or 0 when empty.
    uint32_t firstCycle() const;
    uint32_t lastCycle() const;
    /// Records with cycle in [first, first + count), in order.
    std::vector<TraceCycle> window(uint32_t first, size_t count) const;
    std::vector<TraceCycle> all() const;
    /// The last record, if any.
    bool last(TraceCycle &out) const;

  private:
    mutable std::mutex     m_mutex;
    std::deque<TraceCycle> m_records;
    size_t                 m_capacity;
    uint64_t               m_dropped = 0;
};

/// Sums over a trace for the Profiler (WP-05).
struct TraceTotals
{
    unsigned stallLoadUse       = 0;
    unsigned stallRawNoForward  = 0;
    unsigned stallUnknown       = 0;
    unsigned flushedBranch      = 0; ///< cycles lost: squashed slots, IF included
    unsigned flushedJump        = 0;
    unsigned flushedTrap        = 0; ///< CSR and ecall
};
TraceTotals traceTotals(const std::vector<TraceCycle> &records);
/// Adds (sign +1) or removes (sign -1) one record's share of the totals.
void addToTotals(TraceTotals &totals, const TraceCycle &record, int sign);
} // namespace Atlas
