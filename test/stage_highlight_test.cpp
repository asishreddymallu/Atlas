/**
 * @file stage_highlight_test.cpp
 * @brief The editor's cursor line selects stages in the circuit view.
 *
 * Driven through a real, offscreen MainWindow on the path the toolbar
 * takes: choose h_f, type a program, Debug Run (which assembles, loads and
 * starts the worker paused), then Step/Undo/Redo by triggering the window's
 * own actions, and move the cursor with setTextCursor.
 *
 * The expected result is computed independently: a second h_f processor,
 * owned by the test, is assembled from the same text and stepped the same
 * number of times. Its pipeline labels and line mapping give the selected
 * stages; its active_wires_ / active_wire_stages_ give which wires should
 * be Active, Dimmed or Off. The window's table is checked against it too,
 * so the two machines are known to be in the same cycle.
 *
 * atlas_test does not link the compiled .qrc, so the window's scene is
 * empty until the test loads the h_f drawing into it from disk.
 *
 * Wires Stay Active is switched ON for the tests that look at wire states,
 * so the 100 ms off-timer cannot fire in the middle of an assertion; the
 * off-timer has its own test with it OFF.
 */
#include "float_test_support.h"

#include "processor/processor_types.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/processor_tab/circuit_scene.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QGraphicsView>
#include <QPlainTextEdit>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCursor>

#include <bit>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>

using namespace Atlas;
using namespace Atlas::test_support;

namespace
{
// Line numbers are 1-based, as the editor's labels count them.
const char *kProgram = "addi t0, x0, 5\n"     // 1
                       "li t1, 0x12345\n"     // 2  two instructions
                       "add t2, t0, t0\n"     // 3
                       "sub t3, t2, t0\n"     // 4
                       "# a comment line\n"   // 5  no instruction
                       "addi t4, x0, 1\n"     // 6
                       "addi t5, x0, 2\n"     // 7
                       "addi t6, x0, 3\n"     // 8
                       "addi a0, x0, 4\n"     // 9
                       "addi a1, x0, 5\n";    // 10
constexpr int kFirstLine   = 1;
constexpr int kLiLine      = 2;
constexpr int kAddLine     = 3;
constexpr int kCommentLine = 5;

bool waitUntil(const std::function<bool()> &done, int ms = 3000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        if (clock.elapsed() > ms)
            return false;
        QApplication::processEvents(QEventLoop::AllEvents, 20);
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

/// The test's own h_f, for the expected answer.
struct Reference
{
    std::unique_ptr<ProcessorBase> vm;
    AssembledProgram               program;

    explicit Reference(const QTemporaryDir &dir)
    {
        const std::string path = dir.path().toStdString() + "/ref.s";
        {
            std::ofstream file(path);
            file << kProgram;
        }
        setupVmStateDirectory();
        program = assemble(path);
        vm      = makeProcessor(Model::Rv5HF);
        vm->LoadProgram(program);
    }

    quint8 stagesOnLine(int line) const
    {
        quint8      mask = 0;
        const auto &pcs  = vm->processor_state_.programCounters;
        for (size_t i = 0; i < pcs.size() && i < 5; ++i)
        {
            if (pcs[i] == INVALID_PC)
                continue;
            const auto it = program.instruction_number_line_number_mapping.find(
                static_cast<unsigned int>(pcs[i] / 4));
            if (it != program.instruction_number_line_number_mapping.end() &&
                static_cast<int>(it->second) == line)
                mask |= stageBit(static_cast<WireStage>(i));
        }
        return mask;
    }

    /// name -> expected state for every name the drawing has.
    std::map<QString, WireItem::State> expectedStates(const std::set<QString> &drawn,
                                                      quint8 selected) const
    {
        std::map<QString, quint8> lit;
        for (qsizetype i = 0; i < vm->active_wires_.size(); ++i)
            lit[vm->active_wires_[i]] |= vm->active_wire_stages_.value(i, kNoStage);
        std::map<QString, WireItem::State> out;
        for (const QString &name : drawn)
        {
            const auto it = lit.find(name);
            if (it == lit.end())
                out[name] = WireItem::State::Off;
            else if (selected == 0 || (it->second & selected))
                out[name] = WireItem::State::Active;
            else
                out[name] = WireItem::State::Dimmed;
        }
        return out;
    }
};

const char *stateName(WireItem::State s)
{
    switch (s)
    {
    case WireItem::State::Off:    return "off";
    case WireItem::State::Active: return "ACTIVE";
    case WireItem::State::Dimmed: return "dimmed";
    }
    return "?";
}

struct Bench
{
    MainWindow    window;
    EditorTab    *tab    {nullptr};
    Editor       *editor {nullptr};
    QAction      *debug  {nullptr};
    QAction      *step   {nullptr};
    QAction      *undo   {nullptr};
    QAction      *redo   {nullptr};
    QTemporaryDir dir;

    Bench()
    {
        window.resize(1200, 800);
        window.show();
        QApplication::processEvents();
        window.processorChanged(ProcessorType::RV5Stage_H_F);
        QApplication::processEvents();
        tab = window.findChild<EditorTab *>();
        for (QPlainTextEdit *pane : tab->findChildren<QPlainTextEdit *>())
            if (!pane->isReadOnly())
                editor = static_cast<Editor *>(pane);
        for (QAction *a : window.findChildren<QAction *>(Qt::FindDirectChildrenOnly))
        {
            if (a->text() == "Debug Run") debug = a;
            if (a->text() == "Step")      step  = a;
            if (a->text() == "Undo")      undo  = a;
            if (a->text() == "Redo")      redo  = a;
        }
        loadDrawing();
    }

    CircuitScene *scene() const
    {
        for (QGraphicsView *view : window.findChildren<QGraphicsView *>())
            if (auto *s = dynamic_cast<CircuitScene *>(view->scene()))
                return s;
        return nullptr;
    }

    void loadDrawing()
    {
        if (CircuitScene *s = scene())
            s->loadScene(QStringLiteral("../resources/circuit_designs/H_F_Processor.json"));
    }

    /// Types the program (unless it is already there) and starts a Debug
    /// Run: assembled, loaded, paused. Typing moves the cursor to line 1.
    bool start(bool type = true)
    {
        if (type)
            editor->setPlainText(kProgram);
        QApplication::processEvents();
        debug->trigger();
        return waitUntil([this] { return step->isEnabled(); });
    }

    /// Stops the run the way the toolbar does: the run action reads "Stop"
    /// during a debug run.
    bool stop()
    {
        QAction *run = nullptr;
        for (QAction *a : window.findChildren<QAction *>(Qt::FindDirectChildrenOnly))
            if (a->text() == "Stop")
                run = a;
        if (!run)
            return false;
        run->trigger();
        return waitUntil([this] { return debug->isEnabled(); });
    }

    void stepOnce()
    {
        step->trigger();
        QApplication::processEvents();
    }

    void cursorTo(int line)
    {
        QTextCursor cursor(editor->document()->findBlockByNumber(line - 1));
        editor->setTextCursor(cursor);
        QApplication::processEvents();
    }

    std::set<QString> drawnNames() const
    {
        std::set<QString> names;
        for (QGraphicsItem *item : scene()->items())
            if (auto *w = dynamic_cast<WireItem *>(item))
                if (!w->getName().isEmpty())
                    names.insert(w->getName());
        return names;
    }

    std::map<QString, WireItem::State> states() const
    {
        std::map<QString, WireItem::State> out;
        for (QGraphicsItem *item : scene()->items())
            if (auto *w = dynamic_cast<WireItem *>(item))
                if (!w->getName().isEmpty())
                    out[w->getName()] = w->state();
        return out;
    }

    /// Columns whose cell carries the selected-column background.
    std::string highlightedColumns() const
    {
        std::string out;
        QTableWidget *t = scene()->instructionTable();
        for (int c = 0; c < t->columnCount(); ++c)
            if (t->item(0, c) && t->item(0, c)->background().color() ==
                                     CircuitScene::selectedColumnColor())
                out += (out.empty() ? "" : ",") +
                       t->horizontalHeaderItem(c)->text().toStdString();
        return out.empty() ? "(none)" : out;
    }
};

const char *kColumn[] = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};

std::string columnsFor(quint8 stages)
{
    std::string out;
    for (int s = 1; s <= 4; ++s) // ID..WB have columns; IF has none
        if (stages & (1u << s))
            out += (out.empty() ? "" : ",") + std::string(kColumn[s - 1]);
    return out.empty() ? "(none)" : out;
}

std::string diff(const std::map<QString, WireItem::State> &want,
                 const std::map<QString, WireItem::State> &got)
{
    std::string out;
    for (const auto &[name, state] : want)
    {
        const auto it = got.find(name);
        const WireItem::State g = it == got.end() ? WireItem::State::Off : it->second;
        if (g != state)
            out += "\n  " + name.toStdString() + ": want " + stateName(state) + ", got " +
                   stateName(g);
    }
    return out;
}

/// Steps both machines `n` times.
void stepBoth(Bench &bench, Reference &ref, int n)
{
    for (int i = 0; i < n; ++i)
    {
        bench.stepOnce();
        ref.vm->StepAndNotify();
    }
}
} // namespace

TEST(StageHighlight, TheCursorLineSelectsItsStageAndDimsEveryOtherLitWire)
{
    Bench bench;
    ASSERT_NE(bench.scene(), nullptr);
    ASSERT_TRUE(bench.start()) << "Debug Run never enabled Step";
    bench.scene()->setWireStayActive(true);
    Reference ref(bench.dir);

    // After 4 steps the first addi is in WB, where it lights the writeback
    // wires: something to emphasise, and the backbone to dim.
    stepBoth(bench, ref, 4);
    bench.cursorTo(kFirstLine);

    const quint8 want = ref.stagesOnLine(kFirstLine);
    ASSERT_NE(want, 0) << "the addi is not in the pipeline after 4 steps; the test is mis-set";
    EXPECT_EQ(int(bench.scene()->selectedStages()), int(want));
    EXPECT_EQ(bench.highlightedColumns(), columnsFor(want));

    const auto expected = ref.expectedStates(bench.drawnNames(), want);
    EXPECT_EQ(diff(expected, bench.states()), "");
    int active = 0, dimmed = 0;
    for (const auto &[n, s] : expected)
    {
        active += s == WireItem::State::Active;
        dimmed += s == WireItem::State::Dimmed;
    }
    EXPECT_GT(active, 0) << "nothing emphasised: the comparison would be vacuous";
    EXPECT_GT(dimmed, 0) << "nothing dimmed: the comparison would be vacuous";
}

TEST(StageHighlight, ALineWithNothingInThePipelineSelectsNothing)
{
    Bench bench;
    ASSERT_TRUE(bench.start());
    bench.scene()->setWireStayActive(true);
    Reference ref(bench.dir);
    stepBoth(bench, ref, 5);

    bench.cursorTo(kAddLine);
    ASSERT_NE(bench.scene()->selectedStages(), 0);

    bench.cursorTo(kCommentLine);
    EXPECT_EQ(int(bench.scene()->selectedStages()), 0);
    EXPECT_EQ(bench.highlightedColumns(), "(none)");
    // Nothing selected is today's drawing exactly: every lit wire Active.
    EXPECT_EQ(diff(ref.expectedStates(bench.drawnNames(), 0), bench.states()), "");
}

TEST(StageHighlight, APseudoInstructionLineSelectsEveryStageItsInstructionsAreIn)
{
    Bench bench;
    ASSERT_TRUE(bench.start());
    bench.scene()->setWireStayActive(true);
    Reference ref(bench.dir);
    // Nine instruction lines, one of them li: ten instructions or more.
    ASSERT_GE(ref.program.instruction_number_line_number_mapping.size(), 10u)
        << "li no longer expands to two instructions";

    stepBoth(bench, ref, 3); // li's two halves are in ID and EX
    bench.cursorTo(kLiLine);
    const quint8 want = ref.stagesOnLine(kLiLine);
    EXPECT_GE(std::popcount(unsigned(want)), 2) << "the test is mis-set";
    EXPECT_EQ(int(bench.scene()->selectedStages()), int(want));
    EXPECT_EQ(bench.highlightedColumns(), columnsFor(want));
    EXPECT_EQ(diff(ref.expectedStates(bench.drawnNames(), want), bench.states()), "");
}

TEST(StageHighlight, TheSelectionFollowsEveryStepUndoAndRedo)
{
    Bench bench;
    ASSERT_TRUE(bench.start());
    bench.scene()->setWireStayActive(true);
    Reference ref(bench.dir);

    bench.cursorTo(kAddLine); // the cursor stays here; the add moves
    int changes = 0;
    quint8 last = bench.scene()->selectedStages();
    const auto check = [&](const std::string &when)
    {
        const quint8 want = ref.stagesOnLine(kAddLine);
        EXPECT_EQ(int(bench.scene()->selectedStages()), int(want)) << when;
        EXPECT_EQ(bench.highlightedColumns(), columnsFor(want)) << when;
        EXPECT_EQ(diff(ref.expectedStates(bench.drawnNames(), want), bench.states()), "")
            << when;
        changes += bench.scene()->selectedStages() != last;
        last = bench.scene()->selectedStages();
    };

    for (int i = 1; i <= 7; ++i)
    {
        stepBoth(bench, ref, 1);
        check("after step " + std::to_string(i));
    }
    for (int i = 1; i <= 3; ++i)
    {
        bench.undo->trigger();
        QApplication::processEvents();
        ref.vm->Undo();
        check("after undo " + std::to_string(i));
    }
    for (int i = 1; i <= 2; ++i)
    {
        bench.redo->trigger();
        QApplication::processEvents();
        ref.vm->Redo();
        check("after redo " + std::to_string(i));
    }
    EXPECT_GE(changes, 4) << "the selection hardly moved, so this proved little";
}

TEST(StageHighlight, TheOffTimerStillTurnsEveryWireOffAndTheColumnStays)
{
    Bench bench;
    ASSERT_TRUE(bench.start());
    CircuitScene *scene = bench.scene();
    scene->setWireStayActive(false); // the default
    Reference ref(bench.dir);
    stepBoth(bench, ref, 5);
    bench.cursorTo(kAddLine);
    const quint8 want = ref.stagesOnLine(kAddLine);
    ASSERT_NE(want, 0);

    spin(250); // well past the 100 ms off-timer
    EXPECT_FALSE(scene->wiresShowing());
    for (const auto &[name, state] : bench.states())
        EXPECT_EQ(state, WireItem::State::Off) << name.toStdString() << " survived the off-timer";
    EXPECT_EQ(bench.highlightedColumns(), columnsFor(want)) << "the column must stay selected";

    // A cursor move after the timer relights nothing; it still moves the
    // column.
    bench.cursorTo(kCommentLine);
    for (const auto &[name, state] : bench.states())
        EXPECT_EQ(state, WireItem::State::Off) << name.toStdString() << " was relit by a cursor move";
    EXPECT_EQ(bench.highlightedColumns(), "(none)");

    // And with Wires Stay Active on, the next step's wires stay and the
    // selection restyles them.
    scene->setWireStayActive(true);
    bench.cursorTo(kAddLine);
    stepBoth(bench, ref, 1);
    spin(250);
    const quint8 now = ref.stagesOnLine(kAddLine);
    EXPECT_TRUE(scene->wiresShowing());
    EXPECT_EQ(diff(ref.expectedStates(bench.drawnNames(), now), bench.states()), "");
}

TEST(StageHighlight, AProcessorChangeCarriesTheSelectionIntoTheNewScene)
{
    Bench bench;
    ASSERT_TRUE(bench.start());
    Reference ref(bench.dir);
    stepBoth(bench, ref, 5);
    bench.cursorTo(kAddLine);
    CircuitScene *before = bench.scene();
    ASSERT_NE(before->selectedStages(), 0);

    // Stop, as the toolbar requires before a switch (the Processor action
    // is disabled during a run), then switch: the scene is the
    // processor's, so it is replaced.
    ASSERT_TRUE(bench.stop()) << "Stop did not re-enable Debug Run";
    bench.window.processorChanged(ProcessorType::RV5Stage_H_F);
    QApplication::processEvents();
    bench.loadDrawing();
    CircuitScene *after = bench.scene();
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after, before) << "the switch did not replace the scene";
    // The new processor holds nothing yet, so nothing is selected...
    EXPECT_EQ(int(after->selectedStages()), 0);

    // ...until it runs: the cursor line was kept, and the next step's
    // selection reaches the NEW scene. The program is not retyped, so the
    // cursor is where it was.
    ASSERT_TRUE(bench.start(false));
    after = bench.scene();
    bench.loadDrawing();
    after->setWireStayActive(true);
    Reference fresh(bench.dir);
    stepBoth(bench, fresh, 5);
    EXPECT_EQ(int(after->selectedStages()), int(fresh.stagesOnLine(kAddLine)));
    EXPECT_NE(after->selectedStages(), 0);
}
