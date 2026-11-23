/**
 * @file timing_grid_whole_run_test.cpp
 * @brief T7: the timing grid shows every stored cycle (not the last 40),
 *        its rows in fetch order, and it stays cheap on a long run.
 *
 * The grid the panel keeps up to date step by step is compared with one
 * built from scratch over the whole trace (buildTimingGrid), so the
 * incremental path (append, Undo truncation, ring-buffer front drops)
 * cannot drift from the definition.
 */
#include "processor/execution_trace.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace Atlas;

namespace
{
bool waitUntil(const std::function<bool()> &done, int ms = 60000)
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

QString readExample(const char *name)
{
    QFile f(QString("../examples/cache_lab/%1.s").arg(name));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

struct WholeRunWindow
{
    QTimer           modalTimer;
    MainWindow       window;
    Editor          *editor{nullptr};
    QAction         *run{nullptr}, *debug{nullptr}, *step{nullptr}, *undo{nullptr};
    TimingGridPanel *panel{nullptr};
    QWidget         *overlay{nullptr};

    WholeRunWindow()
    {
        QObject::connect(&modalTimer, &QTimer::timeout, [] {
            if (QWidget *modal = QApplication::activeModalWidget())
                modal->close();
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
        panel   = window.findChild<TimingGridPanel *>("timingGridPanel");
        overlay = window.findChild<QWidget *>("timingDock");
        if (overlay)
            overlay->show();
        window.processorChanged(ProcessorType::RV5Stage_H_F);
        QApplication::processEvents();
    }
    bool ready() const { return editor && run && debug && step && undo && panel && overlay; }
    ProcessorManager *manager() const { return panel->manager(); }
    const TimingGridData &grid() const { return panel->view()->grid(); }

    bool debugStart(const QString &source)
    {
        editor->setPlainText(source);
        QApplication::processEvents();
        debug->trigger();
        return waitUntil([this] { return step->isEnabled(); });
    }
    bool runToEnd(const QString &source)
    {
        editor->setPlainText(source);
        QApplication::processEvents();
        run->trigger();
        const bool done = waitUntil([this] { return debug->isEnabled() && run->text() == "Run"; }, 120000);
        spin(30);
        return done;
    }
    bool jump(uint32_t cycle)
    {
        bool finished = false;
        auto c = QObject::connect(panel, &TimingGridPanel::jumpFinished, [&](uint32_t) { finished = true; });
        panel->jumpToCycle(cycle);
        const bool ok = waitUntil([&] { return finished; });
        QObject::disconnect(c);
        spin(20);
        return ok;
    }
};

/// The first cycle a row is in IF, or 0 when its IF is not in the grid.
uint32_t firstIf(const TimingRow &row)
{
    for (int i = 0; i < row.cells.size(); ++i)
        if (row.cells[i].kind != TimingCell::Empty && row.cells[i].stage == TraceStage::IF)
            return row.firstCycle + static_cast<uint32_t>(i);
    return 0;
}

/// Rows strictly in fetch order: increasing instance id, and the cycles of
/// first fetch (where the grid holds them) strictly increasing.
::testing::AssertionResult inFetchOrder(const TimingGridData &g)
{
    uint32_t lastId = 0, lastIf = 0;
    for (int r = 0; r < g.rows.size(); ++r)
    {
        const TimingRow &row = g.rows[r];
        if (row.id <= lastId)
            return ::testing::AssertionFailure() << "row " << r << " id " << row.id << " after " << lastId;
        lastId = row.id;
        const uint32_t f = firstIf(row);
        if (f == 0)
            continue;
        if (f <= lastIf)
            return ::testing::AssertionFailure() << "row " << r << " (" << row.label.toStdString()
                                                 << ") fetched in " << f << ", not after " << lastIf;
        lastIf = f;
    }
    return ::testing::AssertionSuccess();
}

/// The panel's grid is the one built from scratch over the same records.
::testing::AssertionResult sameGrid(const TimingGridData &got, const TimingGridData &want)
{
    if (got.firstCycle != want.firstCycle || got.lastCycle != want.lastCycle)
        return ::testing::AssertionFailure() << "span " << got.firstCycle << ".." << got.lastCycle << " vs "
                                             << want.firstCycle << ".." << want.lastCycle;
    if (got.rows.size() != want.rows.size())
        return ::testing::AssertionFailure() << got.rows.size() << " rows vs " << want.rows.size();
    for (int r = 0; r < got.rows.size(); ++r)
    {
        const TimingRow &a = got.rows[r], &b = want.rows[r];
        if (a.id != b.id || a.label != b.label || a.squashed != b.squashed)
            return ::testing::AssertionFailure() << "row " << r << " differs";
        // Cell by cell over the row's own cycles (the diagram of a long run
        // is rows x columns tokens, far too many to build).
        const uint32_t from = std::min(a.firstCycle, b.firstCycle);
        const uint32_t to   = std::max(a.lastCycle(), b.lastCycle());
        for (uint32_t c = from; c <= to; ++c)
            if (a.at(c).kind != b.at(c).kind ||
                (a.at(c).kind != TimingCell::Empty && a.at(c).stage != b.at(c).stage))
                return ::testing::AssertionFailure() << "row " << r << " differs in cycle " << c;
    }
    if (got.columns() <= 400 && timingGridDiagram(got) != timingGridDiagram(want))
        return ::testing::AssertionFailure() << "diagrams differ";
    return ::testing::AssertionSuccess();
}

size_t distinctIds(const std::vector<TraceCycle> &records)
{
    std::set<uint32_t> ids;
    for (const TraceCycle &r : records)
        for (const TraceOccupant &o : r.occ)
            if (!o.bubble())
                ids.insert(o.id);
    return ids.size();
}
} // namespace

// The reported view: h_f, 100 cycles in. The first column was 61 and the
// first five rows ran backwards (blt, then addi 60, 59, 58, 57).
TEST(TimingGridWholeRun, AfterA100CycleRunTheFirstColumnIsTheFirstStoredCycle)
{
    WholeRunWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_TRUE(w.debugStart(readExample("matmul_ijk")));
    ASSERT_TRUE(w.jump(100));
    ASSERT_EQ(w.panel->currentCycle(), 100u);
    const TimingGridData &g = w.grid();
    EXPECT_EQ(g.firstCycle, 1u);
    EXPECT_EQ(g.lastCycle, 100u);
    EXPECT_EQ(g.columns(), 100);
    const std::vector<TraceCycle> all = w.manager()->trace().all();
    EXPECT_EQ(static_cast<size_t>(g.rows.size()), distinctIds(all));
    EXPECT_TRUE(inFetchOrder(g));
    EXPECT_TRUE(sameGrid(g, buildTimingGrid(all, w.manager()->currentProgram())));
}

TEST(TimingGridWholeRun, RowsAreInFetchOrderForManyRunLengths)
{
    WholeRunWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_TRUE(w.debugStart(readExample("matmul_ijk")));
    const uint32_t lengths[] = {1,  2,  3,  4,  5,  7,  10, 13,  17,  23,  31,  40,
                                41, 47, 60, 61, 77, 100, 128, 150, 201, 257, 300};
    for (uint32_t n : lengths)
    {
        ASSERT_TRUE(w.jump(n));
        ASSERT_EQ(w.panel->currentCycle(), n);
        EXPECT_EQ(w.grid().firstCycle, 1u) << "length " << n;
        EXPECT_EQ(w.grid().lastCycle, n) << "length " << n;
        EXPECT_TRUE(inFetchOrder(w.grid())) << "length " << n;
    }
    // Back through Undo (a jump), and single Undos.
    for (uint32_t n : {150u, 61u, 37u})
    {
        ASSERT_TRUE(w.jump(n));
        EXPECT_EQ(w.grid().lastCycle, n);
        EXPECT_TRUE(inFetchOrder(w.grid())) << "after jumping back to " << n;
    }
    for (int i = 0; i < 3; ++i)
        w.undo->trigger();
    spin(30);
    EXPECT_EQ(w.grid().lastCycle, 34u);
    EXPECT_TRUE(inFetchOrder(w.grid())) << "after Undo";
}

// Every incremental update (Step, Undo, a jump either way) equals the grid
// built from scratch over the trace.
TEST(TimingGridWholeRun, IncrementalMatchesAFromScratchBuild)
{
    WholeRunWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_TRUE(w.debugStart(readExample("matmul_ijk")));
    auto check = [&](const char *what) {
        const TimingGridData want = buildTimingGrid(w.manager()->trace().all(), w.manager()->currentProgram());
        EXPECT_TRUE(sameGrid(w.grid(), want)) << what << " at cycle " << w.panel->currentCycle();
    };
    for (int i = 0; i < 25; ++i)
    {
        w.step->trigger();
        spin(5);
        check("step");
    }
    for (int i = 0; i < 7; ++i)
    {
        w.undo->trigger();
        spin(5);
        check("undo");
    }
    ASSERT_TRUE(w.jump(240));
    check("jump forward");
    ASSERT_TRUE(w.jump(90));
    check("jump back");
    ASSERT_TRUE(w.jump(91));
    check("one forward");
}

// Ring-buffer overflow: a 64-record trace keeps only the last 64 cycles;
// the grid draws exactly those, in order, and says older ones were dropped.
TEST(TimingGridWholeRun, RingOverflowShowsTheStoredSpanWithANotice)
{
    WholeRunWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_TRUE(w.debugStart(readExample("matmul_ijk")));
    ASSERT_TRUE(w.jump(300));
    const std::vector<TraceCycle> all = w.manager()->trace().all();
    ASSERT_EQ(all.size(), 300u);
    const AssembledProgram &program = w.manager()->currentProgram();

    ExecutionTrace small(64);
    w.panel->useTrace(&small);
    for (size_t i = 0; i < all.size(); ++i)
    {
        small.push(all[i]);
        if (i % 7 == 3 || i + 1 == all.size()) // several cycles per refresh, as during a Run
        {
            w.panel->refresh();
            ASSERT_TRUE(sameGrid(w.grid(), buildTimingGrid(small.all(), program))) << "after cycle " << i + 1;
            ASSERT_TRUE(inFetchOrder(w.grid())) << "after cycle " << i + 1;
        }
    }
    EXPECT_EQ(w.grid().firstCycle, 237u);
    EXPECT_EQ(w.grid().lastCycle, 300u);
    EXPECT_EQ(w.grid().columns(), 64);
    EXPECT_TRUE(w.panel->message().contains(
        "Showing cycles 237 to 300; older cycles were dropped (the trace keeps 64)"))
        << w.panel->message().toStdString();

    // Undo inside the stored span, then forward again.
    small.truncateAfter(280);
    w.panel->refresh();
    EXPECT_TRUE(sameGrid(w.grid(), buildTimingGrid(small.all(), program)));
    EXPECT_EQ(w.grid().lastCycle, 280u);
    for (size_t i = 280; i < all.size(); ++i)
        small.push(all[i]);
    w.panel->refresh();
    EXPECT_TRUE(sameGrid(w.grid(), buildTimingGrid(small.all(), program)));
    EXPECT_TRUE(inFetchOrder(w.grid()));

    // Back on the machine's own trace: the whole run again, no notice.
    w.panel->useTrace(nullptr);
    w.panel->refresh();
    EXPECT_EQ(w.grid().firstCycle, 1u);
    EXPECT_EQ(w.grid().lastCycle, 300u);
    EXPECT_FALSE(w.panel->message().contains("dropped"));
}

// The T3 program (matmul_ijk, 16,843 cycles on h_f): opening the overlay
// after a long run, and a Step with it open, cost what is visible. The
// machine is brought to cycle 16,800 with Debug Run and a jump (a finished
// Run disables Step), the overlay closed meanwhile.
TEST(TimingGridWholeRun, LongRunOpensFastAndStepStaysFast)
{
    WholeRunWindow w;
    ASSERT_TRUE(w.ready());
    w.overlay->hide();
    QApplication::processEvents();
    QElapsedTimer runClock;
    runClock.start();
    ASSERT_TRUE(w.debugStart(readExample("matmul_ijk")));
    ASSERT_TRUE(w.jump(16800));
    const qint64   runMs  = runClock.elapsed();
    const uint32_t cycles = w.panel->currentCycle();
    ASSERT_EQ(cycles, 16800u);

    QElapsedTimer open;
    open.start();
    w.overlay->show();
    w.overlay->repaint();
    QApplication::processEvents();
    const qint64 openMs = open.elapsed();
    ASSERT_EQ(w.grid().lastCycle, cycles);
    EXPECT_EQ(w.grid().firstCycle, 1u);
    EXPECT_TRUE(inFetchOrder(w.grid()));
    EXPECT_LT(openMs, 1000) << "opening the overlay took " << openMs << " ms";

    // Step, then Undo, one at a time, each until the grid shows it.
    qint64    worstUndo = 0, worstStep = 0, totalStep = 0, totalUndo = 0;
    const int n = 20;
    for (int i = 0; i < n; ++i)
    {
        ASSERT_TRUE(w.step->isEnabled());
        QElapsedTimer t;
        t.start();
        w.step->trigger();
        ASSERT_TRUE(waitUntil([&] { return w.grid().lastCycle == w.panel->currentCycle(); }, 5000));
        w.panel->view()->viewport()->repaint();
        worstStep = std::max(worstStep, t.elapsed());
        totalStep += t.elapsed();
    }
    EXPECT_EQ(w.grid().lastCycle, cycles + n);
    for (int i = 0; i < n; ++i)
    {
        QElapsedTimer t;
        t.start();
        w.undo->trigger();
        ASSERT_TRUE(waitUntil([&] { return w.grid().lastCycle == w.panel->currentCycle(); }, 5000));
        w.panel->view()->viewport()->repaint();
        worstUndo = std::max(worstUndo, t.elapsed());
        totalUndo += t.elapsed();
    }
    EXPECT_EQ(w.grid().lastCycle, cycles);
    EXPECT_LT(worstStep, 100) << "a Step with the overlay open took " << worstStep << " ms";
    EXPECT_LT(worstUndo, 100) << "an Undo with the overlay open took " << worstUndo << " ms";

    // From scratch, for the record: the whole trace into a grid.
    QElapsedTimer scratch;
    scratch.start();
    const TimingGridData full = buildTimingGrid(w.manager()->trace().all(), w.manager()->currentProgram());
    const qint64 scratchMs = scratch.elapsed();
    EXPECT_TRUE(sameGrid(w.grid(), full));

    std::printf("[T7 timing] cycles=%u rows=%lld reach=%lld ms open=%lld ms step avg=%.2f worst=%lld ms "
                "undo avg=%.2f worst=%lld ms scratch build=%lld ms\n",
                cycles, static_cast<long long>(w.grid().rows.size()), static_cast<long long>(runMs),
                static_cast<long long>(openMs), static_cast<double>(totalStep) / n,
                static_cast<long long>(worstStep), static_cast<double>(totalUndo) / n,
                static_cast<long long>(worstUndo), static_cast<long long>(scratchMs));
}
