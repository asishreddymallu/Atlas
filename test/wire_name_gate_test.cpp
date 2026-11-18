/**
 * @file wire_name_gate_test.cpp
 * @brief Every wire name a processor lights must be a wire that exists.
 *
 * The circuit view highlights by NAME. Each processor appends strings to
 * `active_wires_`, and `CircuitScene::updateCircuitState`
 * (`circuit_scene.cpp:156-184`) turns on every `WireItem` whose
 * `getName()` is in that list. A name that matches no wire lights
 * nothing, and says nothing: no warning, no log line, no crash. A typo,
 * a reworded drawing, or a scene file half-renamed from another model's
 * all look exactly like "that signal was not active this cycle".
 *
 * This is a lint, written as a test because that is where this project's
 * gates live. It reads the processors' source and the scene JSON from
 * disk rather than asking a running processor, for two reasons: the
 * conditional branches of `SetActiveWireNames` cannot all be reached by
 * any one program, so a run-time union would under-report; and
 * `kites_test` does not link the compiled .qrc, so a `:/circuit_designs`
 * resource path does not resolve here (the same reason
 * `ribbon_navigation_test.cpp` sees null icons). Reading `../resources/...` also checks the file a
 * human edits rather than a copy baked into a binary.
 *
 * Relative paths: ctest runs this with the working directory set to
 * `test/` (`test/CMakeLists.txt:40`), which is how the ~100 existing
 * `"../examples/..."` opens work.
 *
 * The extraction CHECKS ITSELF: the number of `active_wires_.append(`
 * occurrences must equal the number of string-literal matches, so a name
 * built from a variable would fail this file rather than be quietly
 * missed.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>

#include <fstream>
#include <string>

using namespace Kites;
using namespace Kites::test_support;

namespace
{
struct ModelFiles
{
    const char *tag;
    const char *source;
    const char *scene;
};

const ModelFiles kModels[] = {
    {"rvss", "../src/processor/rvss/rvss_processor.cpp", "single_cycle_draft1.json"},
    {"nh-nf", "../src/processor/rv5s/rv5s_processor_nh_nf.cpp", "NH_NF_Processor.json"},
    {"h-nf", "../src/processor/rv5s/rv5s_processor_h_nf.cpp", "H_NF_Processor.json"},
    {"nh-f", "../src/processor/rv5s/rv5s_processor_nh_f.cpp", "NH_F_Processor.json"},
    {"h-f", "../src/processor/rv5s/rv5s_processor_h_f.cpp", "H_F_Processor.json"},
};

struct Appended
{
    bool        read = false;
    QStringList live;      ///< distinct names, sorted, from live code
    int         calls = 0; ///< every `active_wires_.append(` occurrence
    int         literals = 0;
    int         commented = 0;
};

/// Every name the model can put in `active_wires_`.
///
/// The whole file, not just `SetActiveWireNames`: each constructor
/// appends a backbone block and records its length in
/// `always_active_wires_count_`, and `SetActiveWireNames` truncates back
/// to exactly that prefix and keeps it (`rv5s_processor_h_f.cpp:87`
/// resizes; `nh_nf.cpp:78`, `nh_f.cpp:74`, `h_nf.cpp:63` erase).
Appended appendedNames(const QString &path)
{
    Appended out;
    QFile    file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    out.read = true;

    static const QRegularExpression literal(
        QStringLiteral(R"RX(active_wires_\.append\("([^"]+)"\))RX"));

    QSet<QString> seen;
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines)
    {
        out.calls += line.count(QStringLiteral("active_wires_.append("));
        const QRegularExpressionMatch match = literal.match(line);
        if (!match.hasMatch())
            continue;
        ++out.literals;
        // A commented-out append lights nothing. nh-nf has four of these
        // and nh-f one; counting them as live would make the gate assert
        // against names the code cannot reach.
        if (line.trimmed().startsWith(QStringLiteral("//")))
        {
            ++out.commented;
            continue;
        }
        if (!seen.contains(match.captured(1)))
        {
            seen.insert(match.captured(1));
            out.live << match.captured(1);
        }
    }
    out.live.sort();
    return out;
}

struct Scene
{
    bool        read = false;
    QStringList named;   ///< distinct non-empty names, sorted
    int         wires = 0;
    int         unnamed = 0;
};

Scene sceneWires(const QString &file)
{
    Scene out;
    QFile handle(QStringLiteral("../resources/circuit_designs/") + file);
    if (!handle.open(QIODevice::ReadOnly))
        return out;
    const QJsonDocument doc = QJsonDocument::fromJson(handle.readAll());
    if (doc.isNull() || !doc.isObject())
        return out;
    out.read = true;

    QSet<QString> seen;
    for (const QJsonValue &value : doc.object().value(QStringLiteral("wires")).toArray())
    {
        ++out.wires;
        const QString name = value.toObject().value(QStringLiteral("name")).toString();
        if (name.isEmpty())
        {
            ++out.unnamed;
            continue;
        }
        if (!seen.contains(name))
        {
            seen.insert(name);
            out.named << name;
        }
    }
    out.named.sort();
    return out;
}

QStringList difference(const QStringList &from, const QStringList &remove)
{
    const QSet<QString> drop(remove.begin(), remove.end());
    QStringList         out;
    for (const QString &name : from)
        if (!drop.contains(name))
            out << name;
    return out;
}

std::string joined(const QStringList &names)
{
    return names.isEmpty() ? std::string("(none)") : names.join(QStringLiteral(", ")).toStdString();
}

/**
 * H_NF_Processor.json has 27 of its 46 wires with an EMPTY name, so two
 * thirds of what the h-nf processor computes can never light anything.
 * That is data work on the drawing, not a code fix, and this stage edits
 * no JSON - see the known_issues entry.
 *
 * Asserted as an exact set rather than a count, so it fails if the list
 * shrinks (someone started naming wires and should update this) as well
 * as if it grows (someone added an append with no wire). Generated from
 * the files, not typed.
 */
const QStringList kKnownUnmatchedHNf = {
    "ALU1_to_PCMuxUp",     "ALU2_to_P3",          "ALU_to_P3",
    "Control_ControlMux",  "DMMux_to_DM",         "HDUMux_to_P2",
    "HDU_mux_lowest_toP2", "HDU_to_HDU_mUX",      "HDU_to_P1",
    "Imm_to_P2",           "P1_to_Control_RF_andall", "P1_to_P2_PCcarry",
    "P2Imm_to_ALU2_down",  "P2_to_ALU2",          "P2_to_ALUControl",
    "P2_to_P3_MEMControl", "P2_to_P3_WBcontrol",  "P3ALUres_to_DMup",
    "P3_to_P4_WBcontrol",  "P3_to_PCMux",         "P4_wbcontrol_to_WBmux",
    "PCMux_to_PC",         "RF_to_P2_UP",         "RFdown_to_P2",
};

/// Wires that exist in a drawing and that no code path ever lights. Not
/// a defect on its own - a drawing may carry wires the highlighting does
/// not model - but a baseline, so adding a wire without lighting it, or
/// lighting one that used to be dark, is noticed.
const QStringList kUnlitRvss = {"ALUMux_to_ALU", "DM_to_WBmux", "RF_to_ALU_upper"};
const QStringList kUnlitNhNf = {};
const QStringList kUnlitHNf  = {"Control_to_RF", "P2_to_P3",   "P2_to_RF",
                                "P3_to_P4",      "P3_to_UpperEntryANDGATE",
                                "PC_to_Adder",   "RF_to_P2"};
const QStringList kUnlitNhF  = {"AKUMux_to_ALU",        "DM_to_P4",
                                "FMUX3_bottommostinput", "P3_to_UpperEntryANDGATE",
                                "P4DM_to_lastMux",      "P4_ALU_to_WBMuxDowm",
                                "P4_to_RF_regwritecontrol", "rd_P2_to_P3",
                                "rd_P3_to_P4"};
const QStringList kUnlitHF   = {"AKUMux_to_ALU",       "Control_ControlMux",
                                "DM_to_P4",            "FMUX3_bottommostinput",
                                "HDUMux_to_P2_upper",  "HDU_to_PCWrite",
                                "P3_to_UpperEntryANDGATE", "P4ALU_to_WBMuxDown",
                                "P4DM_to_lastMux",     "P4_to_RF_regwritecontrol",
                                "rd_P2_to_P3",         "rd_P3_to_P4"};

const QStringList &unlitFor(const QString &tag)
{
    if (tag == QLatin1String("rvss"))  return kUnlitRvss;
    if (tag == QLatin1String("nh-nf")) return kUnlitNhNf;
    if (tag == QLatin1String("h-nf"))  return kUnlitHNf;
    if (tag == QLatin1String("nh-f"))  return kUnlitNhF;
    return kUnlitHF;
}
} // namespace

TEST(WireNameGate, TheSourceAndSceneFilesAreWhereThisTestExpects)
{
    for (const ModelFiles &model : kModels)
    {
        const Appended appended = appendedNames(QString::fromLatin1(model.source));
        const Scene    scene    = sceneWires(QString::fromLatin1(model.scene));
        EXPECT_TRUE(appended.read) << model.tag << ": cannot read " << model.source
                                   << " - is the working directory test/?";
        EXPECT_TRUE(scene.read) << model.tag << ": cannot read or parse " << model.scene;
        EXPECT_FALSE(appended.live.isEmpty()) << model.tag << " appends no wire names at all";
        EXPECT_GT(scene.wires, 0) << model.scene << " has no wires";
    }
}

TEST(WireNameGate, EveryAppendIsAStringLiteralTheExtractionCanSee)
{
    // Without this the gate could pass by seeing nothing. If a name is
    // ever built at run time the counts diverge and this says so, rather
    // than the gate silently covering less than it claims.
    for (const ModelFiles &model : kModels)
    {
        const Appended appended = appendedNames(QString::fromLatin1(model.source));
        ASSERT_TRUE(appended.read) << model.tag;
        EXPECT_EQ(appended.calls, appended.literals)
            << model.tag << ": " << appended.calls << " calls to active_wires_.append( but "
            << appended.literals
            << " string-literal arguments - a name this gate cannot see is being appended";
    }
}

TEST(WireNameGate, EveryAppendedNameIsANamedWireInThatModelsScene)
{
    for (const ModelFiles &model : kModels)
    {
        const Appended appended = appendedNames(QString::fromLatin1(model.source));
        const Scene    scene    = sceneWires(QString::fromLatin1(model.scene));
        ASSERT_TRUE(appended.read && scene.read) << model.tag;

        const QStringList unmatched = difference(appended.live, scene.named);

        if (QLatin1String(model.tag) == QLatin1String("h-nf"))
        {
            EXPECT_EQ(unmatched, kKnownUnmatchedHNf)
                << "h-nf's known-unmatched set changed.\n  now: " << joined(unmatched)
                << "\n  was: " << joined(kKnownUnmatchedHNf)
                << "\n  (H_NF_Processor.json leaves " << scene.unnamed
                << " of its " << scene.wires << " wires unnamed; see known_issues)";
            continue;
        }

        EXPECT_TRUE(unmatched.isEmpty())
            << model.tag << " lights " << unmatched.size() << " name(s) that no wire in "
            << model.scene << " carries: " << joined(unmatched);
    }
}

TEST(WireNameGate, TheDrawnButNeverLitWiresAreTheRecordedBaseline)
{
    for (const ModelFiles &model : kModels)
    {
        const Appended appended = appendedNames(QString::fromLatin1(model.source));
        const Scene    scene    = sceneWires(QString::fromLatin1(model.scene));
        ASSERT_TRUE(appended.read && scene.read) << model.tag;

        const QStringList unlit = difference(scene.named, appended.live);
        EXPECT_EQ(unlit, unlitFor(QLatin1String(model.tag)))
            << model.tag << ": the drawn-but-never-lit set changed.\n  now (" << unlit.size()
            << "): " << joined(unlit) << "\n  was (" << unlitFor(QLatin1String(model.tag)).size()
            << "): " << joined(unlitFor(QLatin1String(model.tag)));
    }
}

TEST(WireNameGate, TheSceneFilesStillHaveTheWireCountsThisBaselineAssumes)
{
    // The baselines above are only meaningful against the drawings they
    // were measured from. nh-f carries one unnamed wire and h-nf 27.
    const struct
    {
        const char *scene;
        int         wires;
        int         named;
        int         unnamed;
    } expected[] = {
        {"single_cycle_draft1.json", 22, 22, 0}, {"NH_NF_Processor.json", 44, 44, 0},
        {"H_NF_Processor.json", 46, 19, 27},     {"NH_F_Processor.json", 53, 52, 1},
        {"H_F_Processor.json", 57, 57, 0},
    };

    for (const auto &row : expected)
    {
        const Scene scene = sceneWires(QString::fromLatin1(row.scene));
        ASSERT_TRUE(scene.read) << row.scene;
        EXPECT_EQ(scene.wires, row.wires) << row.scene << ": wire count";
        EXPECT_EQ(scene.named.size(), row.named) << row.scene << ": distinct named wires";
        EXPECT_EQ(scene.unnamed, row.unnamed) << row.scene << ": wires with an empty name";
    }
}

TEST(WireNameGate, OnlyTheSingleCycleModelFailsToTruncateItsWireList)
{
    // Every model's SetActiveWireNames is supposed to start from the
    // constructor's backbone and add this cycle's wires to it. The four
    // pipelined ones do: h_f resizes (rv5s_processor_h_f.cpp:87), the
    // other three erase back to always_active_wires_count_
    // (nh_nf.cpp:78, nh_f.cpp:74, h_nf.cpp:63).
    //
    // RVSSProcessor::SetActiveWireNames does not - its clear is
    // commented out (rvss_processor.cpp:63) - so calling it twice leaves
    // this cycle's wires on the list twice. Its one caller that gets
    // away with that is StepAndNotify, which erases afterwards
    // (:1206-1207). DebugRun (:1124), Undo (:1274) and Redo (:1342) do
    // not, and Undo and Redo are toolbar buttons.
    //
    // A program has to be loaded and stepped first: on a processor that
    // has executed nothing, every control-unit getter is false and rvss
    // appends nothing, so the growth is invisible.
    //
    // Pinned, not fixed: the one-line fix changes what the diagram
    // draws, which wants its own stage. See the known_issues entry.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : allModels())
    {
        const std::string path = dir.path().toStdString() + "/wg" +
                                 std::to_string(static_cast<int>(model)) + ".s";
        {
            std::ofstream file(path);
            file << ".text\n    addi t0, x0, 7\n    add s0, t0, t0\n";
        }
        setupVmStateDirectory();
        auto vm = makeProcessor(model);
        ASSERT_NE(vm, nullptr) << name(model);
        vm->LoadProgram(assemble(path));
        vm->StepAndNotify(); // so the control signals describe a real instruction

        vm->SetActiveWireNames();
        const qsizetype once = vm->active_wires_.size();
        vm->SetActiveWireNames();
        const qsizetype twice = vm->active_wires_.size();

        if (model == Model::Rvss)
        {
            EXPECT_GT(twice, once)
                << "rvss stopped growing (" << once << " -> " << twice
                << "). If its clear was restored, this test and the known_issues entry both "
                   "need updating";
        }
        else
        {
            EXPECT_EQ(twice, once) << name(model) << " grew across two calls: " << once << " -> "
                                   << twice;
        }
    }
}
