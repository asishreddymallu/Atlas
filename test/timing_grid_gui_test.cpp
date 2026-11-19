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
#include "processor/processor_types.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>

#include <functional>
#include <string>

using namespace Kites;

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
    QDockWidget     *dock{nullptr};

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
        dock  = window.findChild<QDockWidget *>("timingDock");
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

// Every stage colour keeps the theme's text at WCAG AA (4.5:1) in all four
// built-in themes. The test binary has no Qt resources, so the themes are
// read from the source tree (ctest runs in test/).
TEST(TimingGridColours, StageCellsAreAaInEveryTheme)
{
    QDir dir("../resources/themes");
    const QStringList files = dir.entryList({"*.json"}, QDir::Files);
    ASSERT_EQ(files.size(), 4) << dir.absolutePath().toStdString();
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
