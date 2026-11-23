#include "processor/trace_explain.h"

#include <algorithm>

namespace Atlas
{
namespace
{
std::string reg(RegFile file, uint8_t r)
{
    return (file == RegFile::X ? "x" : "f") + std::to_string(r);
}

std::string cycles(size_t n)
{
    return std::to_string(n) + (n == 1 ? " stall cycle" : " stall cycles");
}

const char *stageDoes(TraceStage s)
{
    switch (s)
    {
    case TraceStage::IF: return "is fetched from instruction memory";
    case TraceStage::ID: return "is decoded and reads its registers";
    case TraceStage::EX: return "executes in the ALU";
    case TraceStage::MEM: return "is in the memory stage (loads and stores access data memory here)";
    case TraceStage::WB: return "writes its result back to the register file";
    }
    return "";
}

const char *ahead(TraceStage producerStage)
{
    return producerStage == TraceStage::EX ? "one instruction ahead" : "two instructions ahead";
}
} // namespace

std::string modelPhrase(const ExplainModel &m)
{
    if (m.hazardDetection && m.forwarding)
        return "hazard detection + forwarding";
    if (m.hazardDetection)
        return "hazard detection, no forwarding";
    if (m.forwarding)
        return "forwarding, no hazard detection";
    return "no hazard detection, no forwarding";
}

std::string explainTraceCell(const std::vector<TraceCycle> &records, size_t index, uint32_t id,
                             const ExplainModel &model, const std::function<std::string(uint64_t)> &label)
{
    if (index >= records.size())
        return {};
    const TraceCycle &r = records[index];
    const std::string at = "Cycle " + std::to_string(r.cycle) + ": ";
    int               stage = -1;
    for (size_t s = 0; s < kTraceStages; ++s)
        if (!r.occ[s].bubble() && r.occ[s].id == id)
            stage = static_cast<int>(s);
    if (stage < 0)
        return at + "this instruction is not in the pipeline.";
    const TraceOccupant &o    = r.occ[static_cast<size_t>(stage)];
    const std::string    self = label(o.pc);
    const std::string    on   = "On this processor (" + modelPhrase(model) + ")";

    if (r.singleCycle)
        return at + self + " runs from fetch to write-back in this one cycle (single-cycle processor).";

    // Flushed by a redirect this cycle.
    if (std::find(r.redirect.squashed.begin(), r.redirect.squashed.end(), o) != r.redirect.squashed.end())
    {
        const std::string by   = label(r.redirect.byPc);
        const size_t      lost = r.redirect.squashed.size();
        std::string       why;
        switch (r.redirect.kind)
        {
        case RedirectKind::Branch:
            why = by + ", a taken branch, reached MEM and sent fetch to its target";
            break;
        case RedirectKind::Jump: why = by + ", a jump, reached EX and sent fetch to its target"; break;
        case RedirectKind::Csr: why = by + ", a CSR instruction, was served in WB and fetch restarts after it"; break;
        case RedirectKind::Ecall: why = by + ", an ecall, was served in WB and fetch restarts after it"; break;
        case RedirectKind::None: break;
        }
        return at + self + " is flushed from " + traceStageName(static_cast<TraceStage>(stage)) + ": " + why +
               ". The " + std::to_string(lost) + " slots behind it are discarded, so " + std::to_string(lost) +
               " cycles are lost.";
    }

    // Held in ID by the hazard unit (only when the machine stalled).
    if (stage == 1 && r.stalled)
    {
        // The stall run this instruction is in, as the trace recorded it.
        size_t first = index, last = index;
        while (first > 0 && records[first - 1].stalled && records[first - 1].occ[1].id == id)
            --first;
        while (last + 1 < records.size() && records[last + 1].stalled && records[last + 1].occ[1].id == id)
            ++last;
        const TraceStall &st    = records[first].stall;
        const size_t      run   = last - first + 1;
        const size_t      which = index - first + 1;
        const std::string part  = run > 1 ? " (stall cycle " + std::to_string(which) + " of " +
                                               std::to_string(run) + ")"
                                         : "";
        const std::string prod  = label(st.producerPc);
        switch (st.reason)
        {
        case StallReason::LoadUse:
            return at + self + " waits in ID because " + prod + ", " + ahead(st.producerStage) +
                   ", has not loaded " + reg(st.file, st.reg) + " yet. " + on + " this costs " + cycles(run) +
                   part + ".";
        case StallReason::RawNoForwarding:
            return at + self + " waits in ID because " + prod + ", " + ahead(st.producerStage) +
                   ", has not written " + reg(st.file, st.reg) + " back yet and nothing forwards it. " + on +
                   " this costs " + cycles(run) + part + ".";
        default:
            return at + self + " is held in ID by the hazard unit; the trace has no dependence that explains it.";
        }
    }

    // In ID with a dependence the hazard unit does not stall for.
    if (stage == 1 && !r.raw.empty())
    {
        const TraceRaw   &d    = r.raw.front();
        const std::string prod = label(d.producerPc);
        const std::string rr   = reg(d.file, d.reg);
        if (d.resolution == RawResolution::Forward)
            return at + self + " reads " + rr + ", which " + prod + ", " + ahead(d.producerStage) +
                   ", has not written back yet. " + on + " the forwarding unit hands it the value in EX, so it "
                   "does not wait.";
        if (d.resolution == RawResolution::Unhandled)
        {
            if (model.forwarding)
                return at + self + " needs " + rr + " from " + prod + ", a load one instruction ahead. " + on +
                       " nothing waits for the load, and its value is only ready at the end of MEM, so " + self +
                       " gets a wrong value. Put 1 nop between them.";
            const int pad = d.producerStage == TraceStage::EX ? 2 : 1;
            return at + self + " reads " + rr + " in ID before " + prod + ", " + ahead(d.producerStage) +
                   ", has written it back. " + on + " nothing waits, so it reads the old value. Put " +
                   std::to_string(pad) + (pad == 1 ? " nop" : " nops") + " between them.";
        }
    }

    // In EX, with operands taken from the forwarding paths.
    if (stage == 2 && !r.fwd.empty())
    {
        std::string s = at + self + " executes in the ALU; ";
        for (size_t i = 0; i < r.fwd.size(); ++i)
        {
            const TraceForward &f = r.fwd[i];
            s += (i ? "; " : "") + reg(f.file, f.reg) + " (rs" + std::to_string(f.operand) + ") comes from " +
                 label(f.producerPc) + " over the " + (f.from == TraceStage::MEM ? "EX/MEM" : "MEM/WB") +
                 " forwarding path";
            if (f.producerIsLoad && f.from == TraceStage::MEM)
                s += ", but a load in MEM has only its address there, so the value is wrong (put 1 nop "
                     "between them)";
        }
        return s + ".";
    }

    // In IF, its fetch held because the instruction ahead is stalled.
    if (stage == 0 && r.postIds[0] == id)
        return at + self + " is fetched but stays in IF: the instruction ahead of it is held in ID.";

    return at + self + " " + stageDoes(static_cast<TraceStage>(stage)) + ".";
}
} // namespace Atlas
