#include "processor/trace_json.h"

#include <QJsonArray>
#include <QJsonValue>

#include <sstream>

namespace Atlas
{
namespace
{
const char *kSchema = "atlas.trace/1";

QString hex(uint64_t v, int width)
{
    return QStringLiteral("0x%1").arg(v, width, 16, QLatin1Char('0'));
}
uint64_t unhex(const QJsonValue &v) { return v.toString().mid(2).toULongLong(nullptr, 16); }

QString regName(RegFile file, uint8_t reg)
{
    return (file == RegFile::X ? QStringLiteral("x") : QStringLiteral("f")) + QString::number(reg);
}
void parseReg(const QString &s, RegFile &file, uint8_t &reg)
{
    file = s.startsWith('f') ? RegFile::F : RegFile::X;
    reg  = static_cast<uint8_t>(s.mid(1).toUInt());
}

TraceStage stageOf(const QString &s)
{
    for (size_t i = 0; i < kTraceStages; ++i)
        if (s == QLatin1String(traceStageName(static_cast<TraceStage>(i))))
            return static_cast<TraceStage>(i);
    return TraceStage::IF;
}

QJsonValue occupantJson(const TraceOccupant &o, const AssembledProgram *program)
{
    if (o.bubble())
        return QJsonValue::Null;
    QJsonObject j;
    j["id"]          = static_cast<qint64>(o.id);
    j["pc"]          = hex(o.pc, 8);
    j["instruction"] = hex(o.instruction, 8);
    if (program)
    {
        j["line"]        = traceSourceLine(*program, o.pc);
        j["disassembly"] = traceDisassembly(*program, o.pc);
    }
    return j;
}
TraceOccupant occupantFrom(const QJsonValue &v)
{
    TraceOccupant o;
    if (!v.isObject())
        return o;
    const QJsonObject j = v.toObject();
    o.id                = static_cast<uint32_t>(j["id"].toInteger());
    o.pc                = unhex(j["pc"]);
    o.instruction       = static_cast<uint32_t>(unhex(j["instruction"]));
    return o;
}

// A producer named by id, pc and word only (no source lookup needed to
// identify it; the occupant object of the same id carries the text).
QJsonObject producerJson(uint32_t id, uint64_t pc, uint32_t instruction)
{
    QJsonObject j;
    j["id"]          = static_cast<qint64>(id);
    j["pc"]          = hex(pc, 8);
    j["instruction"] = hex(instruction, 8);
    return j;
}
} // namespace

int traceSourceLine(const AssembledProgram &program, uint64_t pc)
{
    const auto it = program.instruction_number_line_number_mapping.find(static_cast<unsigned>(pc / 4));
    return it == program.instruction_number_line_number_mapping.end() ? -1 : static_cast<int>(it->second);
}

QString traceDisassembly(const AssembledProgram &program, uint64_t pc)
{
    const size_t n = static_cast<size_t>(pc / 4);
    if (n >= program.intermediate_code.size())
        return {};
    std::ostringstream os;
    os << program.intermediate_code[n].first;
    return QString::fromStdString(os.str()).trimmed();
}

namespace
{
// The word of instance `id`: the stage occupant with that id in record
// `index`, else in the nearest earlier record. TraceForward and
// TraceRedirect name a producer by id and pc only. 0 when no record holds
// it (it left the window the trace keeps).
uint32_t wordOf(const std::vector<TraceCycle> &records, size_t index, uint32_t id)
{
    if (id == 0)
        return 0;
    for (size_t k = index + 1; k-- > 0;)
        for (const TraceOccupant &o : records[k].occ)
            if (o.id == id)
                return o.instruction;
    return 0;
}
} // namespace

QJsonObject traceToJson(const std::vector<TraceCycle> &records, const TraceMeta &meta,
                        const AssembledProgram &program)
{
    QJsonObject root;
    root["schema"]           = kSchema;
    root["model"]            = meta.model;
    root["hazard_detection"] = meta.hazardDetection;
    root["forwarding"]       = meta.forwarding;
    root["single_cycle"]     = meta.singleCycle;
    root["capacity"]         = static_cast<qint64>(meta.capacity);
    root["dropped"]          = static_cast<qint64>(meta.dropped);

    QJsonArray cycles;
    for (size_t index = 0; index < records.size(); ++index)
    {
        const TraceCycle &r = records[index];
        QJsonObject       c;
        c["cycle"] = static_cast<qint64>(r.cycle);
        QJsonObject stages;
        if (r.singleCycle)
            stages["CI"] = occupantJson(r.occ[0], &program);
        else
            for (size_t s = 0; s < kTraceStages; ++s)
                stages[traceStageName(static_cast<TraceStage>(s))] = occupantJson(r.occ[s], &program);
        c["stages"] = stages;

        if (r.stalled)
        {
            QJsonObject st;
            st["reason"]         = stallReasonName(r.stall.reason);
            st["continuing"]     = r.stall.continuing;
            st["length"]         = r.stall.length;
            st["producer_stage"] = traceStageName(r.stall.producerStage);
            st["producer"] = producerJson(r.stall.producerId, r.stall.producerPc, r.stall.producerInstruction);
            st["register"] = regName(r.stall.file, r.stall.reg);
            c["stall"]     = st;
        }
        else
            c["stall"] = QJsonValue::Null;

        QJsonArray raw;
        for (const TraceRaw &x : r.raw)
        {
            QJsonObject j;
            j["register"]         = regName(x.file, x.reg);
            j["producer_stage"]   = traceStageName(x.producerStage);
            j["producer"]         = producerJson(x.producerId, x.producerPc, x.producerInstruction);
            j["producer_is_load"] = x.producerIsLoad;
            j["resolution"]       = rawResolutionName(x.resolution);
            raw.append(j);
        }
        c["raw"] = raw;

        QJsonArray fwd;
        for (const TraceForward &f : r.fwd)
        {
            QJsonObject j;
            j["operand"]          = QStringLiteral("rs%1").arg(f.operand);
            j["register"]         = regName(f.file, f.reg);
            j["path"]             = f.from == TraceStage::MEM ? "EX/MEM" : "MEM/WB";
            j["producer"]         = producerJson(f.producerId, f.producerPc, wordOf(records, index, f.producerId));
            j["producer_is_load"] = f.producerIsLoad;
            fwd.append(j);
        }
        c["forwarding"] = fwd;

        if (r.redirect.kind != RedirectKind::None)
        {
            QJsonObject rd;
            rd["kind"] = redirectKindName(r.redirect.kind);
            rd["by"]   = producerJson(r.redirect.byId, r.redirect.byPc, wordOf(records, index, r.redirect.byId));
            QJsonArray sq;
            for (const TraceOccupant &o : r.redirect.squashed)
                sq.append(occupantJson(o, &program));
            rd["squashed"] = sq;
            c["redirect"]  = rd;
        }
        else
            c["redirect"] = QJsonValue::Null;

        QJsonArray post;
        for (uint32_t id : r.postIds)
            post.append(static_cast<qint64>(id));
        c["post_ids"]       = post;
        c["next_id_before"] = static_cast<qint64>(r.nextIdBefore);
        c["next_id_after"]  = static_cast<qint64>(r.nextIdAfter);
        cycles.append(c);
    }
    root["cycles"] = cycles;
    return root;
}

bool traceFromJson(const QJsonObject &root, std::vector<TraceCycle> &records, TraceMeta &meta)
{
    if (root["schema"].toString() != QLatin1String(kSchema))
        return false;
    meta.model           = root["model"].toString();
    meta.hazardDetection = root["hazard_detection"].toBool();
    meta.forwarding      = root["forwarding"].toBool();
    meta.singleCycle     = root["single_cycle"].toBool();
    meta.capacity        = static_cast<size_t>(root["capacity"].toInteger());
    meta.dropped         = static_cast<uint64_t>(root["dropped"].toInteger());

    records.clear();
    for (const QJsonValue &cv : root["cycles"].toArray())
    {
        const QJsonObject c = cv.toObject();
        TraceCycle        r;
        r.cycle             = static_cast<uint32_t>(c["cycle"].toInteger());
        r.singleCycle       = meta.singleCycle;
        const QJsonObject s = c["stages"].toObject();
        if (r.singleCycle)
            r.occ[0] = occupantFrom(s["CI"]);
        else
            for (size_t i = 0; i < kTraceStages; ++i)
                r.occ[i] = occupantFrom(s[traceStageName(static_cast<TraceStage>(i))]);

        if (c["stall"].isObject())
        {
            const QJsonObject st = c["stall"].toObject();
            r.stalled            = true;
            const QString reason = st["reason"].toString();
            for (auto k : {StallReason::LoadUse, StallReason::RawNoForwarding, StallReason::Unknown})
                if (reason == QLatin1String(stallReasonName(k)))
                    r.stall.reason = k;
            r.stall.continuing          = st["continuing"].toBool();
            r.stall.length              = static_cast<uint8_t>(st["length"].toInt());
            r.stall.producerStage       = stageOf(st["producer_stage"].toString());
            const TraceOccupant p       = occupantFrom(st["producer"]);
            r.stall.producerId          = p.id;
            r.stall.producerPc          = p.pc;
            r.stall.producerInstruction = p.instruction;
            parseReg(st["register"].toString(), r.stall.file, r.stall.reg);
        }
        for (const QJsonValue &xv : c["raw"].toArray())
        {
            const QJsonObject j = xv.toObject();
            TraceRaw          x;
            parseReg(j["register"].toString(), x.file, x.reg);
            x.producerStage       = stageOf(j["producer_stage"].toString());
            const TraceOccupant p = occupantFrom(j["producer"]);
            x.producerId          = p.id;
            x.producerPc          = p.pc;
            x.producerInstruction = p.instruction;
            x.producerIsLoad      = j["producer_is_load"].toBool();
            const QString res     = j["resolution"].toString();
            for (auto k : {RawResolution::Stall, RawResolution::Forward, RawResolution::Unhandled})
                if (res == QLatin1String(rawResolutionName(k)))
                    x.resolution = k;
            r.raw.push_back(x);
        }
        for (const QJsonValue &fv : c["forwarding"].toArray())
        {
            const QJsonObject j = fv.toObject();
            TraceForward      f;
            f.operand = static_cast<uint8_t>(j["operand"].toString().mid(2).toUInt());
            parseReg(j["register"].toString(), f.file, f.reg);
            f.from                = j["path"].toString() == QLatin1String("EX/MEM") ? TraceStage::MEM : TraceStage::WB;
            const TraceOccupant p = occupantFrom(j["producer"]);
            f.producerId          = p.id;
            f.producerPc          = p.pc;
            f.producerIsLoad      = j["producer_is_load"].toBool();
            r.fwd.push_back(f);
        }
        if (c["redirect"].isObject())
        {
            const QJsonObject rd = c["redirect"].toObject();
            const QString     k  = rd["kind"].toString();
            for (auto kind : {RedirectKind::Branch, RedirectKind::Jump, RedirectKind::Csr, RedirectKind::Ecall})
                if (k == QLatin1String(redirectKindName(kind)))
                    r.redirect.kind = kind;
            const TraceOccupant by = occupantFrom(rd["by"]);
            r.redirect.byId        = by.id;
            r.redirect.byPc        = by.pc;
            for (const QJsonValue &o : rd["squashed"].toArray())
                r.redirect.squashed.push_back(occupantFrom(o));
        }
        const QJsonArray post = c["post_ids"].toArray();
        for (size_t i = 0; i < kTraceStages && i < static_cast<size_t>(post.size()); ++i)
            r.postIds[i] = static_cast<uint32_t>(post[static_cast<qsizetype>(i)].toInteger());
        r.nextIdBefore = static_cast<uint32_t>(c["next_id_before"].toInteger());
        r.nextIdAfter  = static_cast<uint32_t>(c["next_id_after"].toInteger());
        records.push_back(r);
    }
    return true;
}
} // namespace Atlas
