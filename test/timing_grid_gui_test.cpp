/**
 * @file timing_grid_gui_test.cpp
 * @brief WP-03: the timing grid in the real window, on all four pipelines,
 *        against the same hand-drawn diagrams as execution_trace_test.cpp.
 *
 * The program is typed into the editor and driven with the window's own
 * Run, Debug Run, Step and Undo actions, offscreen, as gui_run_smoke_test
 * does. The grid is read back in the diagrams' notation
 * (timingGridDiagram), so it is compared cell by cell with the drawing.
 */
#include "processor/memory_controller.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "processor/registers.h"
#include "profiler/profiler.h"
#include "common/instruction_types.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTableView>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <ostream>
#include <string>
#include <vector>

using namespace Atlas;

namespace
{
bool waitUntil(const std::function<bool()> &done, int ms = 15000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        if (clock.elapsed() > ms)
            return false;
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void spin(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

struct GridWindow
{
    QTimer           modalTimer;
    QStringList      modals;
    MainWindow       window;
    Editor          *editor{nullptr};
    QAction         *run{nullptr}, *debug{nullptr}, *step{nullptr}, *undo{nullptr};
    TimingGridPanel *panel{nullptr};
    QWidget         *dock{nullptr}; ///< the timing overlay (T6; was a QDockWidget)

    GridWindow()
    {
        QObject::connect(&modalTimer, &QTimer::timeout, [this] {
            if (QWidget *modal = QApplication::activeModalWidget())
            {
                auto *box = qobject_cast<QMessageBox *>(modal);
                modals << (box ? box->text() : modal->windowTitle());
                modal->close();
            }
        });
        modalTimer.start(50);
        window.resize(1200, 800);
        window.show();
        QApplication::processEvents();
        auto *tab = window.findChild<EditorTab *>();
        for (QPlainTextEdit *pane : tab->findChildren<QPlainTextEdit *>())
            if (!pane->isReadOnly())
                editor = static_cast<Editor *>(pane);
        for (QAction *a : window.findChildren<QAction *>(Qt::FindDirectChildrenOnly))
        {
            if (a->text() == "Run") run = a;
            if (a->text() == "Debug Run") debug = a;
            if (a->text() == "Step") step = a;
            if (a->text() == "Undo") undo = a;
        }
        for (QSpinBox *spinBox : window.findChildren<QSpinBox *>())
            if (spinBox->suffix() == " ms")
                spinBox->setValue(1);
        panel = window.findChild<TimingGridPanel *>("timingGridPanel");
        dock  = window.findChild<QWidget *>("timingDock");
        if (dock)
            dock->show();
    }
    bool ready() const { return editor && run && debug && step && undo && panel && dock; }

    void use(ProcessorType t)
    {
        window.processorChanged(t);
        QApplication::processEvents();
    }
    bool runToEnd(const char *source)
    {
        editor->setPlainText(QString::fromUtf8(source));
        QApplication::processEvents();
        run->trigger();
        const bool done = waitUntil([this] { return debug->isEnabled() && run->text() == "Run"; });
        spin(30); // the queued refresh
        return done;
    }
    bool debugStart(const char *source)
    {
        editor->setPlainText(QString::fromUtf8(source));
        QApplication::processEvents();
        debug->trigger();
        return waitUntil([this] { return step->isEnabled(); });
    }
    QStringList diagram() const { return timingGridDiagram(panel->view()->grid()); }
    bool        jump(uint32_t cycle)
    {
        bool finished = false;
        auto c = QObject::connect(panel, &TimingGridPanel::jumpFinished, [&](uint32_t) { finished = true; });
        panel->jumpToCycle(cycle);
        const bool ok = waitUntil([&] { return finished; });
        QObject::disconnect(c);
        spin(30);
        return ok;
    }
};

const char *kLoadUse = ".text\n    lw x5, 0(x0)\n    add x6, x5, x5\n    addi x7, x0, 1\n";
const char *kBranch  = ".text\n    beq x0, x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\n"
                       "    addi x7, x0, 3\nT:  addi x8, x0, 4\n";
const char *kJal = ".text\n    jal x0, T\n    addi x5, x0, 1\n    addi x6, x0, 2\nT:  addi x7, x0, 3\n";

// The hand-drawn diagrams (execution_trace_test.cpp states the rules).
const QStringList kNoStall   = {"IF ID EX ME WB", ". IF ID EX ME WB", ". . IF ID EX ME WB"};
const QStringList kTwoStalls = {"IF ID EX ME WB", ". IF ID* ID* ID EX ME WB", ". . IF IF IF ID EX ME WB"};
const QStringList kOneStall  = {"IF ID EX ME WB", ". IF ID* ID EX ME WB", ". . IF IF ID EX ME WB"};
const QStringList kBranchRows = {"IF ID EX ME WB", ". IF ID EXx", ". . IF IDx", ". . . IFx",
                                 ". . . . IF ID EX ME WB"};
const QStringList kJalRows = {"IF ID EX ME WB", ". IF IDx", ". . IFx", ". . . IF ID EX ME WB"};

struct Pipe
{
    const char       *name;
    ProcessorType     type;
    const QStringList *loadUse;
};
const Pipe kPipes[] = {{"nh_nf", ProcessorType::RV5Stage_NH_NF, &kNoStall},
                       {"nh_f", ProcessorType::RV5Stage_NH_F, &kNoStall},
                       {"h_nf", ProcessorType::RV5Stage_H_NF, &kTwoStalls},
                       {"h_f", ProcessorType::RV5Stage_H_F, &kOneStall}};

class TimingGridGui : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(TimingGridGui, TheGridIsTheHandDrawnDiagram)
{
    const Pipe &p = kPipes[GetParam()];
    GridWindow  w;
    ASSERT_TRUE(w.ready());
    w.use(p.type);
    ASSERT_TRUE(w.runToEnd(kLoadUse));
    EXPECT_EQ(w.diagram(), *p.loadUse) << w.diagram().join("\n").toStdString();
    ASSERT_TRUE(w.runToEnd(kBranch));
    EXPECT_EQ(w.diagram(), kBranchRows) << w.diagram().join("\n").toStdString();
    ASSERT_TRUE(w.runToEnd(kJal));
    EXPECT_EQ(w.diagram(), kJalRows) << w.diagram().join("\n").toStdString();
    EXPECT_EQ(w.panel->view()->grid().rows.size(), 4);
    EXPECT_TRUE(w.panel->view()->grid().rows[1].squashed);
    EXPECT_TRUE(w.modals.isEmpty()) << w.modals.join(" | ").toStdString();
}

// Part C: selecting a cell shows the trace's sentence for it.
TEST_P(TimingGridGui, SelectingACellExplainsIt)
{
    const Pipe &p = kPipes[GetParam()];
    GridWindow  w;
    ASSERT_TRUE(w.ready());
    w.use(p.type);
    ASSERT_TRUE(w.runToEnd(kLoadUse));
    w.panel->view()->select(1, 2); // add x6, x5, x5 in cycle 3
    const QString text = w.panel->explanation();
    EXPECT_TRUE(text.startsWith("Cycle 3: add x6, x5, x5 ")) << text.toStdString();
    const bool stalled = p.type == ProcessorType::RV5Stage_H_NF || p.type == ProcessorType::RV5Stage_H_F;
    EXPECT_EQ(text.contains("stall cycle"), stalled) << text.toStdString();
    if (!stalled)
        EXPECT_TRUE(text.contains("nop") || text.contains("forwarding unit")) << text.toStdString();
}

TEST_P(TimingGridGui, UndoRemovesTheLastColumn)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kPipes[GetParam()].type);
    ASSERT_TRUE(w.debugStart(kLoadUse));
    for (int i = 0; i < 5; ++i)
        w.step->trigger();
    spin(30);
    ASSERT_EQ(w.panel->view()->grid().columns(), 5);
    w.undo->trigger();
    spin(30);
    EXPECT_EQ(w.panel->view()->grid().columns(), 4);
    EXPECT_EQ(w.panel->currentCycle(), 4u);
    // The four columns left are the first four of the full drawing.
    QStringList want;
    for (const QString &row : *kPipes[GetParam()].loadUse)
    {
        QStringList cells = row.split(' ').mid(0, 4);
        while (!cells.isEmpty() && cells.back() == ".")
            cells.removeLast();
        if (!cells.isEmpty())
            want << cells.join(' ');
    }
    EXPECT_EQ(w.diagram(), want) << w.diagram().join("\n").toStdString();
}

TEST_P(TimingGridGui, JumpToCycleMovesTheMachine)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kPipes[GetParam()].type);
    ASSERT_TRUE(w.debugStart(kBranch));
    ASSERT_TRUE(w.jump(6));
    EXPECT_EQ(w.panel->currentCycle(), 6u);
    EXPECT_EQ(w.panel->view()->grid().columns(), 6);
    ASSERT_TRUE(w.jump(2)); // back, through Undo
    EXPECT_EQ(w.panel->currentCycle(), 2u);
    EXPECT_EQ(w.panel->view()->grid().columns(), 2);
    ASSERT_TRUE(w.jump(9)); // forward again, to the end
    EXPECT_EQ(w.panel->currentCycle(), 9u);
    EXPECT_EQ(w.diagram(), kBranchRows) << w.diagram().join("\n").toStdString();
    ASSERT_TRUE(w.jump(50)); // past the end: stops at the end
    EXPECT_EQ(w.panel->currentCycle(), 9u);
}

INSTANTIATE_TEST_SUITE_P(Pipelines, TimingGridGui, ::testing::Values(0, 1, 2, 3),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kPipes[info.param].name);
                         });

TEST(TimingGridGuiRvss, SaysItIsSingleCycle)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RVSS);
    ASSERT_TRUE(w.runToEnd(kLoadUse));
    EXPECT_TRUE(w.panel->message().startsWith("Single-cycle processor")) << w.panel->message().toStdString();
    EXPECT_EQ(w.panel->view()->grid().rows.size(), 0);
}

TEST(TimingGridGuiExport, FileExportTraceWritesTheRun)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RV5Stage_H_F);
    ASSERT_TRUE(w.runToEnd(kLoadUse));
    QAction *action = w.window.findChild<QAction *>("exportTraceAction");
    ASSERT_NE(action, nullptr);
    EXPECT_EQ(action->text(), "Export trace...");
    QTemporaryDir dir;
    const QString path = dir.filePath("t.json");
    ASSERT_TRUE(w.window.exportTrace(path));
    QFile f(path);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly));
    const QJsonObject j = QJsonDocument::fromJson(f.readAll()).object();
    EXPECT_EQ(j["model"].toString(), "h_f");
    EXPECT_EQ(j["cycles"].toArray().size(), 8);
}

// Every stage colour keeps the theme's text at WCAG AA (4.5:1) in every
// built-in theme (ten since T8). The test binary has no Qt resources, so the themes are
// read from the source tree (ctest runs in test/).
TEST(TimingGridColours, StageCellsAreAaInEveryTheme)
{
    QDir dir("../resources/themes");
    const QStringList files = dir.entryList({"*.json"}, QDir::Files);
    ASSERT_EQ(files.size(), 10) << dir.absolutePath().toStdString();
    for (const QString &name : files)
    {
        QFile f(dir.filePath(name));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QJsonObject colors = QJsonDocument::fromJson(f.readAll()).object()["colors"].toObject();
        const QColor surface(colors["surface"].toString());
        const QColor text(colors["foreground"].toString());
        ASSERT_TRUE(surface.isValid() && text.isValid()) << name.toStdString();
        for (int s = 0; s < 5; ++s)
        {
            const auto   stage  = static_cast<TraceStage>(s);
            const QColor accent(colors[timingStageToken(stage)].toString());
            ASSERT_TRUE(accent.isValid()) << name.toStdString() << " " << timingStageToken(stage);
            const TimingCellColors c = timingCellColorsFrom(accent, surface, text);
            EXPECT_GE(contrastRatio(c.text, c.fill), 4.5)
                << name.toStdString() << " " << timingStageToken(stage) << ": text " << c.text.name().toStdString()
                << " on " << c.fill.name().toStdString();
        }
    }
}

// ---- T3: jumping forward after Run / Undo, on all five models ------------
// Forward moves must use Redo while the machine has undone cycles to give
// back, and Step only for the rest: after Run to the end, Step is disabled,
// so a jump that only knows Step cannot move forward at all.
namespace
{
struct JumpModel
{
    const char   *name;
    ProcessorType type;
};
const JumpModel kJumpModels[] = {{"rvss", ProcessorType::RVSS},
                                 {"nh_nf", ProcessorType::RV5Stage_NH_NF},
                                 {"nh_f", ProcessorType::RV5Stage_NH_F},
                                 {"h_nf", ProcessorType::RV5Stage_H_NF},
                                 {"h_f", ProcessorType::RV5Stage_H_F}};

// 24 stores into a 96-byte buffer at 0x10000, padded with nops so all five
// models compute the same values (nh_nf has no hazard unit). Well over 80
// cycles on every model, RVSS included.
constexpr uint64_t kBufBase  = 0x10000;
constexpr int      kBufBytes = 96;
const char *kStoreLoop = ".text\n"
                         "    lui x10, 0x10\n"
                         "    addi x11, x0, 24\n"
                         "    addi x0, x0, 0\n"
                         "    addi x0, x0, 0\n"
                         "L:  sw x11, 0(x10)\n"
                         "    addi x10, x10, 4\n"
                         "    addi x11, x11, -1\n"
                         "    addi x0, x0, 0\n"
                         "    addi x0, x0, 0\n"
                         "    bne x11, x0, L\n"
                         "    addi x12, x0, 7\n";

struct MachineState
{
    std::vector<uint64_t> gpr, fpr;
    std::vector<uint8_t>  mem;
    uint64_t              pc{};
    unsigned              cycles{}, retired{};
    bool                  operator==(const MachineState &) const = default;
};

MachineState capture(ProcessorManager *m)
{
    MachineState s;
    for (size_t i = 0; i < 32; ++i)
    {
        s.gpr.push_back(m->getRegisterFile()->ReadGpr(i));
        s.fpr.push_back(m->getRegisterFile()->ReadFpr(i));
    }
    for (int i = 0; i < kBufBytes; ++i)
        s.mem.push_back(m->getMemoryController()->peekByte(kBufBase + i));
    s.pc      = m->getProgramCounter();
    s.cycles  = m->getCycles();
    s.retired = m->getInstructionsRetired();
    return s;
}

std::string describe(const MachineState &s)
{
    std::string out = "cycles " + std::to_string(s.cycles) + " retired " + std::to_string(s.retired) + " pc " +
                      std::to_string(s.pc) + " x10 " + std::to_string(s.gpr[10]) + " x11 " +
                      std::to_string(s.gpr[11]) + " x12 " + std::to_string(s.gpr[12]);
    return out;
}

void PrintTo(const MachineState &s, std::ostream *os)
{
    *os << describe(s);
}

QByteArray exportBytes(GridWindow &w, const QString &path)
{
    if (!w.window.exportTrace(path))
        return {};
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

class TimingGridJump : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(TimingGridJump, RunThenBackToTheMiddleThenForwardToTheEnd)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kJumpModels[GetParam()].type);
    ASSERT_TRUE(w.runToEnd(kStoreLoop));
    ProcessorManager *m = w.panel->manager();
    ASSERT_NE(m, nullptr);
    const MachineState end = capture(m);
    ASSERT_GT(end.cycles, 80u);
    EXPECT_EQ(end.gpr[12], 7u) << "the program ran to its last instruction";
    QTemporaryDir     dir;
    const QByteArray  before = exportBytes(w, dir.filePath("before.json"));
    ASSERT_FALSE(before.isEmpty());

    ASSERT_TRUE(w.jump(end.cycles / 2));
    EXPECT_EQ(w.panel->currentCycle(), end.cycles / 2);
    ASSERT_TRUE(w.jump(end.cycles));
    EXPECT_EQ(w.panel->currentCycle(), end.cycles) << "forward again, through Redo";
    const MachineState again = capture(m);
    EXPECT_EQ(again, end) << describe(again) << " vs " << describe(end);

    // The round trip leaves the trace as it was.
    const QByteArray after = exportBytes(w, dir.filePath("after.json"));
    EXPECT_EQ(after.size(), before.size());
    EXPECT_TRUE(after == before) << "the exported trace changed over the round trip";
    EXPECT_EQ(m->trace().dropped(), 0u);
}

TEST_P(TimingGridJump, PartialRedoThenStepMatchesAStraightRun)
{
    MachineState jumped, straight;
    QByteArray   jumpedTrace, straightTrace;
    QTemporaryDir dir;
    {
        GridWindow w;
        ASSERT_TRUE(w.ready());
        w.use(kJumpModels[GetParam()].type);
        ASSERT_TRUE(w.debugStart(kStoreLoop));
        for (int i = 0; i < 60; ++i)
            w.step->trigger();
        spin(30);
        ASSERT_EQ(w.panel->currentCycle(), 60u);
        for (int i = 0; i < 30; ++i)
            w.undo->trigger();
        spin(30);
        ASSERT_EQ(w.panel->currentCycle(), 30u);
        ASSERT_TRUE(w.jump(80)); // 30 Redos, then 20 Steps
        EXPECT_EQ(w.panel->currentCycle(), 80u);
        jumped      = capture(w.panel->manager());
        jumpedTrace = exportBytes(w, dir.filePath("jumped.json"));
    }
    {
        GridWindow w;
        ASSERT_TRUE(w.ready());
        w.use(kJumpModels[GetParam()].type);
        ASSERT_TRUE(w.debugStart(kStoreLoop));
        for (int i = 0; i < 80; ++i)
            w.step->trigger();
        spin(30);
        ASSERT_EQ(w.panel->currentCycle(), 80u);
        straight      = capture(w.panel->manager());
        straightTrace = exportBytes(w, dir.filePath("straight.json"));
    }
    EXPECT_EQ(jumped, straight) << describe(jumped) << " vs " << describe(straight);
    ASSERT_FALSE(straightTrace.isEmpty());
    EXPECT_TRUE(jumpedTrace == straightTrace) << "the trace after the jump differs from a straight run";
}

TEST_P(TimingGridJump, ForwardClampsAndNoOps)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kJumpModels[GetParam()].type);
    ASSERT_TRUE(w.runToEnd(kStoreLoop));
    ProcessorManager  *m    = w.panel->manager();
    const MachineState end  = capture(m);
    const uint32_t     last = end.cycles;

    // At the end: forward past it, and to the current cycle, change nothing.
    ASSERT_TRUE(w.jump(last + 50));
    EXPECT_EQ(w.panel->currentCycle(), last);
    EXPECT_EQ(capture(m), end);
    ASSERT_TRUE(w.jump(last));
    EXPECT_EQ(capture(m), end);

    // From the middle: to the current cycle is a no-op; past the end clamps.
    ASSERT_TRUE(w.jump(last / 3));
    ASSERT_EQ(w.panel->currentCycle(), last / 3);
    const MachineState mid = capture(m);
    ASSERT_TRUE(w.jump(last / 3));
    EXPECT_EQ(capture(m), mid);
    ASSERT_TRUE(w.jump(last + 100));
    EXPECT_EQ(w.panel->currentCycle(), last) << "clamped to the last cycle";
    EXPECT_EQ(capture(m), end);
}

TEST_P(TimingGridJump, ForwardOnANeverRunProgramSteps)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kJumpModels[GetParam()].type);
    ASSERT_TRUE(w.debugStart(kStoreLoop));
    ASSERT_EQ(w.panel->currentCycle(), 0u);
    ASSERT_TRUE(w.jump(25)); // nothing to Redo: all Steps
    EXPECT_EQ(w.panel->currentCycle(), 25u);
}

INSTANTIATE_TEST_SUITE_P(Models, TimingGridJump, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kJumpModels[info.param].name);
                         });

// ---- T4: the explanation under the grid never outlives its trace ----------
// Seen in the hand-test: select the stalled cell after a Run on h_f, switch
// to another model and Run again, and the h_f sentence stayed under the grid.
namespace
{
// lw needs x10 from lui; the addi needs x12 from the lw (a load-use pair).
const char *kExplainProgram = ".text\n"
                              "    lui x10, 0x10\n"
                              "    lw x12, 0(x10)\n"
                              "    addi x13, x12, 1\n";

/// The first stalled cell of the row whose instruction starts with `label`.
bool findStalled(const TimingGridData &g, const QString &label, int &row, int &col)
{
    for (int r = 0; r < g.rows.size(); ++r)
    {
        if (!g.rows[r].label.startsWith(label))
            continue;
        for (int c = 0; c < g.rows[r].cells.size(); ++c)
            if (g.rows[r].cells[c].kind == TimingCell::Stalled)
            {
                row = r;
                col = static_cast<int>(g.rows[r].firstCycle - g.firstCycle) + c; // cells are the row's own cycles
                return true;
            }
    }
    return false;
}

/// Run on h_f and select the addi's stalled cell; returns the sentence.
QString selectHfStall(GridWindow &w)
{
    w.use(ProcessorType::RV5Stage_H_F);
    if (!w.runToEnd(kExplainProgram))
        return {};
    int row = -1, col = -1;
    if (!findStalled(w.panel->view()->grid(), "addi x13", row, col))
        return {};
    w.panel->view()->select(row, col);
    return w.panel->explanation();
}

const JumpModel kOtherModels[] = {{"h_nf", ProcessorType::RV5Stage_H_NF},
                                  {"nh_f", ProcessorType::RV5Stage_NH_F},
                                  {"nh_nf", ProcessorType::RV5Stage_NH_NF},
                                  {"rvss", ProcessorType::RVSS}};

class TimingGridExplain : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(TimingGridExplain, ANewRunOnAnotherModelClearsTheExplanation)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    const QString before = selectHfStall(w);
    ASSERT_TRUE(before.contains("hazard detection + forwarding")) << before.toStdString();
    ASSERT_TRUE(before.contains("1 stall cycle")) << before.toStdString();
    w.use(kOtherModels[GetParam()].type);
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << "after the switch: " << w.panel->explanation().toStdString();
    ASSERT_TRUE(w.runToEnd(kExplainProgram));
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << "after the Run: " << w.panel->explanation().toStdString();
    EXPECT_EQ(w.panel->view()->selectedRow(), -1);
    EXPECT_EQ(w.panel->view()->selectedColumn(), -1);
}

INSTANTIATE_TEST_SUITE_P(Models, TimingGridExplain, ::testing::Values(0, 1, 2, 3),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kOtherModels[info.param].name);
                         });

TEST(TimingGridExplainReset, ASecondRunOnTheSameModelClearsIt)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_FALSE(selectHfStall(w).isEmpty());
    ASSERT_TRUE(w.runToEnd(kExplainProgram)); // the same program again: a new trace
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << w.panel->explanation().toStdString();
    EXPECT_EQ(w.panel->view()->selectedRow(), -1);
}

TEST(TimingGridExplainReset, ReloadingTheProgramClearsIt)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_FALSE(selectHfStall(w).isEmpty());
    ASSERT_TRUE(w.debugStart(kExplainProgram)); // reloads: back to cycle 0
    spin(30);
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << w.panel->explanation().toStdString();
    EXPECT_EQ(w.panel->view()->selectedRow(), -1);
}

TEST(TimingGridExplainReset, StoppingADebugRunClearsIt)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RV5Stage_H_F);
    ASSERT_TRUE(w.debugStart(kExplainProgram));
    for (int i = 0; i < 6; ++i)
        w.step->trigger();
    spin(30);
    int row = -1, col = -1;
    ASSERT_TRUE(findStalled(w.panel->view()->grid(), "addi x13", row, col));
    w.panel->view()->select(row, col);
    ASSERT_FALSE(w.panel->explanation().isEmpty());
    ASSERT_EQ(w.run->text(), "Stop");
    w.run->trigger(); // Stop resets the machine
    spin(30);
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << w.panel->explanation().toStdString();
    EXPECT_EQ(w.panel->view()->selectedRow(), -1);
}

TEST(TimingGridExplainReset, UndoPastTheSelectedCycleClearsIt)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RV5Stage_H_F);
    ASSERT_TRUE(w.debugStart(kExplainProgram));
    for (int i = 0; i < 7; ++i)
        w.step->trigger();
    spin(30);
    int row = -1, col = -1;
    ASSERT_TRUE(findStalled(w.panel->view()->grid(), "addi x13", row, col));
    const uint32_t selectedCycle = w.panel->view()->grid().firstCycle + col;
    w.panel->view()->select(row, col);
    const QString text = w.panel->explanation();
    ASSERT_FALSE(text.isEmpty());
    // One Undo that keeps the selected cycle keeps the sentence.
    w.undo->trigger();
    spin(30);
    ASSERT_GE(w.panel->currentCycle(), selectedCycle);
    EXPECT_EQ(w.panel->explanation(), text);
    // Undo until the selected cycle is gone.
    while (w.panel->currentCycle() >= selectedCycle)
    {
        w.undo->trigger();
        spin(10);
    }
    spin(30);
    EXPECT_TRUE(w.panel->explanation().isEmpty()) << w.panel->explanation().toStdString();
    EXPECT_EQ(w.panel->view()->selectedRow(), -1);
}

TEST(TimingGridExplainReset, HnfStallNamesTheModelAndItsLength)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RV5Stage_H_NF);
    ASSERT_TRUE(w.runToEnd(kExplainProgram));
    int row = -1, col = -1;
    ASSERT_TRUE(findStalled(w.panel->view()->grid(), "addi x13", row, col));
    w.panel->view()->select(row, col);
    const QString text = w.panel->explanation();
    EXPECT_TRUE(text.contains("hazard detection, no forwarding")) << text.toStdString();
    // No forwarding: the addi reads x12 in ID in the lw's WB cycle, 2 bubbles.
    EXPECT_TRUE(text.contains("2 stall cycles")) << text.toStdString();
    EXPECT_FALSE(text.contains("hazard detection + forwarding")) << text.toStdString();
}

// ---- T4: the Profiler's Instruction Type table counts the run -------------
// Seen in the hand-test: on a loop the table counted each source line once
// (the program), while Instructions retired and the editor's per-line
// margin counted every retire (the run). The margin's per-line counts are
// the Profiler's (it owns them, known_issues 85); the table must be their
// sum by type.
namespace
{
const char *kTypeLoop = ".text\n"
                        "    lui x10, 0x10\n"
                        "    addi x11, x0, 8\n"
                        "    addi x0, x0, 0\n"
                        "    addi x0, x0, 0\n"
                        "L:  sw x11, 0(x10)\n"
                        "    addi x11, x11, -1\n"
                        "    addi x0, x0, 0\n"
                        "    addi x0, x0, 0\n"
                        "    bne x11, x0, L\n"
                        "    addi x12, x0, 9\n";

using TypeCounts = std::vector<size_t>;

/// The Instruction Type table as shown: one count per type row.
TypeCounts tableCounts(GridWindow &w)
{
    TypeCounts out;
    auto *view = w.window.findChild<QTableView *>("instructionTypeCountTableView");
    if (!view || !view->model())
        return out;
    QAbstractItemModel *m = view->model();
    for (int r = 0; r < m->rowCount(); ++r)
        out.push_back(m->data(m->index(r, 1)).toString().toULongLong());
    return out;
}

/// The margin's numbers summed by type: the Profiler's per-line counts.
TypeCounts marginCounts(ProcessorManager *m)
{
    TypeCounts out(instruction_set::kInstructionTypeCount, 0);
    const Profiler *p     = m->getProfiler();
    const auto      types = p->getLineInstructionTypes();
    for (const auto &[line, count] : p->getLineExecutionCounts())
    {
        const auto it = types.find(line);
        if (it != types.end() && toIndex(it->second) < out.size())
            out[toIndex(it->second)] += static_cast<size_t>(count);
    }
    return out;
}

size_t sum(const TypeCounts &c)
{
    size_t s = 0;
    for (size_t v : c)
        s += v;
    return s;
}

std::string show(const TypeCounts &c)
{
    std::string s;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i])
            s += instruction_set::instructionTypeNames[i] + "=" + std::to_string(c[i]) + " ";
    return s;
}

class ProfilerTypeTable : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(ProfilerTypeTable, AfterARunTheTableIsTheMarginSummedByType)
{
    GridWindow w;
    ASSERT_TRUE(w.ready());
    w.use(kJumpModels[GetParam()].type);
    ASSERT_TRUE(w.runToEnd(kTypeLoop));
    ProcessorManager *m = w.panel->manager();
    const TypeCounts table  = tableCounts(w);
    const TypeCounts margin = marginCounts(m);
    ASSERT_EQ(table.size(), margin.size());
    EXPECT_EQ(sum(table), m->getInstructionsRetired()) << "table " << show(table);
    EXPECT_EQ(table, margin) << "table " << show(table) << " | margin " << show(margin);
}

TEST_P(ProfilerTypeTable, UndoAndRedoMatchAStraightRun)
{
    constexpr int kSteps = 40, kBack = 15;
    TypeCounts    afterUndo, afterRedo, straightShort, straightLong;
    {
        GridWindow w;
        ASSERT_TRUE(w.ready());
        w.use(kJumpModels[GetParam()].type);
        ASSERT_TRUE(w.debugStart(kTypeLoop));
        for (int i = 0; i < kSteps; ++i)
            w.step->trigger();
        spin(30);
        for (int i = 0; i < kBack; ++i)
            w.undo->trigger();
        spin(30);
        afterUndo = tableCounts(w);
        EXPECT_EQ(sum(afterUndo), w.panel->manager()->getInstructionsRetired()) << show(afterUndo);
        EXPECT_EQ(afterUndo, marginCounts(w.panel->manager())) << show(afterUndo);
        for (QAction *a : w.window.findChildren<QAction *>(Qt::FindDirectChildrenOnly))
            if (a->text() == "Redo")
                for (int i = 0; i < kBack; ++i)
                    a->trigger();
        spin(30);
        ASSERT_EQ(w.panel->currentCycle(), static_cast<uint32_t>(kSteps));
        afterRedo = tableCounts(w);
    }
    for (int target : {kSteps - kBack, kSteps})
    {
        GridWindow w;
        ASSERT_TRUE(w.ready());
        w.use(kJumpModels[GetParam()].type);
        ASSERT_TRUE(w.debugStart(kTypeLoop));
        for (int i = 0; i < target; ++i)
            w.step->trigger();
        spin(30);
        (target == kSteps ? straightLong : straightShort) = tableCounts(w);
    }
    EXPECT_EQ(afterUndo, straightShort) << show(afterUndo) << " vs " << show(straightShort);
    EXPECT_EQ(afterRedo, straightLong) << show(afterRedo) << " vs " << show(straightLong);
}

INSTANTIATE_TEST_SUITE_P(Models, ProfilerTypeTable, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kJumpModels[info.param].name);
                         });
