/**
 * @file gui_run_smoke_test.cpp
 * @brief Drive the real window's Run, Debug Run, Step, Undo, Redo, Stop and
 *        processor switch on the programs E1-E4 made runnable, on all five
 *        models.
 *
 * The Run abort (a CSR instruction indexing the profiler's type table out
 * of bounds) was in code that only the window reaches:
 * `MainWindow::run` -> `tryParseAndLoadProgram` -> `ProcessorManager::
 * loadProgram` -> `Profiler::setLineNumberToInstructionTypeMapping`
 * (`main_window.cpp:933`, `:911`; `processor_manager.cpp:139`). The engine
 * tests that ran the same CSR programs called the processor directly and
 * never passed through the profiler. This test goes through the window:
 * it types into the editor and triggers the window's own actions, offscreen.
 *
 * Every expected register value is worked out by hand from the ISA, in the
 * comment beside it, not copied from a run.
 *
 * Models without hazard detection (nh_nf, nh_f) get four nops after every
 * instruction. That is enough for any producer to reach writeback before
 * its consumer decodes, so the expected values are the same on all five.
 * nh_nf still cannot run li/la of a large value: it is one source line
 * that expands to a dependent chain the source cannot pad (known_issues
 * 77). Those two programs are checked only for finishing on nh_nf.
 *
 * The ecall read has no input path in the window. Nothing in src/ui calls
 * ProcessorManager::pushInput (known_issues 140), so the read waits until
 * Stop. That case checks the wait and the stop, not the read.
 */
#include "processor/processor_types.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/main_window/main_window.h"
#include "ui/register_table/register_model.h"

#include <gtest/gtest.h>

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QPlainTextEdit>
#include <QSpinBox>

#include <cstdint>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

bool hasHazardDetection(ProcessorType t)
{
    return t == ProcessorType::RVSS || t == ProcessorType::RV5Stage_H_NF ||
           t == ProcessorType::RV5Stage_H_F;
}

std::string modelName(ProcessorType t)
{
    switch (t)
    {
    case ProcessorType::RVSS: return "rvss";
    case ProcessorType::RV5Stage_NH_NF: return "nh_nf";
    case ProcessorType::RV5Stage_H_NF: return "h_nf";
    case ProcessorType::RV5Stage_NH_F: return "nh_f";
    case ProcessorType::RV5Stage_H_F: return "h_f";
    default: return "other";
    }
}

/// Four nops after every instruction line (not after labels, directives,
/// data or blank lines) on a model without hazard detection.
std::string padFor(ProcessorType t, const std::string &source)
{
    if (hasHazardDetection(t))
        return source;
    std::istringstream in(source);
    std::string        out, line;
    bool               inText = false;
    while (std::getline(in, line))
    {
        out += line + "\n";
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        const std::string body = line.substr(first);
        if (body.rfind(".text", 0) == 0)
            inText = true;
        else if (body.rfind(".data", 0) == 0)
            inText = false;
        if (!inText || body[0] == '.' || body[0] == '#' || body.back() == ':')
            continue;
        out += "    nop\n    nop\n    nop\n    nop\n";
    }
    return out;
}

struct Program
{
    const char *name;
    const char *source;
    /// GPR index -> expected 64-bit value.
    std::map<int, uint64_t> expected;
    /// False: nh_nf is only required to finish (a li/la chain).
    bool valuesOnNhNf = true;
};

// x-register numbers by ABI name, for the expectations.
constexpr int t1 = 6, a0 = 10, a1 = 11, a2 = 12, a3 = 13, a4 = 14, a7 = 17;

const std::vector<Program> &programs()
{
    static const std::vector<Program> p = {
        {"CrashProgram",
         // The program from the user's backtrace, line for line.
         ".text\n"
         "    li t0, 1\n"
         "    fcvt.s.w f1, t0\n"   // 1.0
         "    fcvt.s.w f2, x0\n"   // +0.0
         "    fdiv.s f3, f1, f2\n" // 1/+0 = +inf, raises DZ only
         "    fmv.x.w a0, f3\n"    // +inf single = 0x7F800000, sign bit 0
         "    csrrs a1, fflags, x0\n" // DZ is fflags bit 3 = 0x08
         "    fclass.s a3, f3\n"   // +inf is class bit 7 = 0x80
         "    csrwi frm, 1\n"
         "    csrr a2, frm\n",     // 1 (RTZ)
         // fflags accrue: the fmv.x.w between the fdiv and the read raises
         // nothing and clears nothing, so a1 is still DZ.
         {{a0, 0x7F800000u}, {a1, 0x08}, {a2, 1}, {a3, 0x80}}},
        {"DivByZeroRaisesDz",
         ".text\n"
         "    li t0, 1\n"
         "    fcvt.s.w f1, t0\n"   // 1.0
         "    fcvt.s.w f2, x0\n"   // +0.0
         "    fdiv.s f3, f1, f2\n" // 1/+0: DZ, fflags bit 3
         "    csrrs a1, fflags, x0\n", // 0x08
         {{a1, 0x08}}},
        {"EcallPrintThenExit",
         ".text\n"
         "    li a0, 42\n"
         "    li a7, 1\n"
         "    ecall\n"           // print_int 42
         "    li a0, 7\n"
         "    li a7, 10\n"
         "    ecall\n"           // exit: nothing after it runs
         "    li t1, 5\n",       // so t1 stays 0
         {{a0, 7}, {a7, 10}, {t1, 0}}},
        {"CsrForms",
         ".text\n"
         "    li t0, 2\n"
         "    csrrw t2, frm, t0\n"   // t2 = old frm 0; frm = 2
         "    csrr t3, frm\n"        // 2
         "    csrwi fflags, 5\n"     // fflags = 0b00101
         "    csrrs t4, fflags, x0\n"// 5
         "    csrw fflags, x0\n"
         "    csrr t5, fflags\n"     // 0
         "    csrrwi t6, frm, 0\n",  // t6 = 2; frm = 0
         {{7, 0}, {28, 2}, {29, 5}, {30, 0}, {31, 2}}},
        {"FclassAndNegativeDivByZero",
         ".text\n"
         "    fcvt.s.w f1, x0\n"     // +0.0
         "    fsgnjn.s f2, f1, f1\n" // -0.0
         "    fclass.s a0, f1\n"     // +0 is bit 4 = 0x10
         "    fclass.s a1, f2\n"     // -0 is bit 3 = 0x08
         "    li t0, -1\n"
         "    fcvt.s.w f3, t0\n"     // -1.0
         "    fdiv.s f4, f3, f1\n"   // -1/+0 = -inf
         "    fclass.s a2, f4\n"     // -inf is bit 0 = 0x01
         "    fmv.x.w a3, f4\n",     // 0xFF800000 sign-extended from bit 31
         {{a0, 0x10}, {a1, 0x08}, {a2, 0x01}, {a3, 0xFFFFFFFFFF800000ull}}},
        {"WordForms",
         ".text\n"
         "    li t0, 1\n"
         "    slli t0, t0, 31\n"     // 0x0000000080000000
         "    addw a0, t0, x0\n"     // low word 0x80000000 sign-extended
         "    addiw a1, t0, -1\n"    // 0x80000000 - 1 = 0x7FFFFFFF, bit 31 clear
         "    li t1, 1\n"
         "    li t2, 31\n"
         "    sllw a2, t1, t2\n"     // 1 << 31 = 0x80000000, sign-extended
         "    sraiw a3, a0, 4\n"     // 0x80000000 >>a 4 = 0xF8000000, sign-extended
         "    subw a4, x0, t1\n",    // -1
         {{a0, 0xFFFFFFFF80000000ull},
          {a1, 0x7FFFFFFFull},
          {a2, 0xFFFFFFFF80000000ull},
          {a3, 0xFFFFFFFFF8000000ull},
          {a4, 0xFFFFFFFFFFFFFFFFull}}},
        {"CallAndRet",
         ".text\n"
         "    call F\n"
         "    li a1, 2\n"            // runs after the return
         "    j END\n"
         "F:\n"
         "    li a0, 1\n"
         "    ret\n"
         "END:\n"
         "    addi a2, x0, 3\n",
         {{a0, 1}, {a1, 2}, {a2, 3}}},
        {"LiLargestPositiveWord",
         ".text\n"
         "    li a0, 0x7FFFFFFF\n",  // exactly 2^31 - 1 (known_issues 110)
         {{a0, 0x7FFFFFFFull}},
         false},
        {"LaOfADataLabel",
         ".data\n"
         "V: .word 0x1234\n"
         ".text\n"
         "    la t0, V\n"
         "    lw a0, 0(t0)\n",      // the word stored at V
         {{a0, 0x1234}},
         false},
    };
    return p;
}

const std::vector<ProcessorType> &allTypes()
{
    static const std::vector<ProcessorType> t = {
        ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_H_NF,
        ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_H_F};
    return t;
}

/// A modal box (the window's QMessageBox::critical on an assemble error,
/// say) runs its own event loop and would hang an offscreen test. This
/// closes any modal it sees, every 50 ms, and keeps its text for the test
/// to fail on.
struct ModalCatcher
{
    QTimer      timer;
    QStringList seen;
    ModalCatcher()
    {
        QObject::connect(&timer, &QTimer::timeout, [this] {
            if (QWidget *modal = QApplication::activeModalWidget())
            {
                if (auto *box = qobject_cast<QMessageBox *>(modal))
                    seen << box->text();
                else
                    seen << modal->windowTitle();
                modal->close();
            }
        });
        timer.start(50);
    }
};

struct Window
{
    ModalCatcher  modals;
    MainWindow    window;
    Editor       *editor{nullptr};
    QAction      *run{nullptr};
    QAction      *debug{nullptr};
    QAction      *step{nullptr};
    QAction      *undo{nullptr};
    QAction      *redo{nullptr};
    RegisterModel *registers{nullptr};

    Window()
    {
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
            if (a->text() == "Redo") redo = a;
        }
        registers = window.findChild<RegisterModel *>();
        // The step delay defaults to 1000 ms per cycle.
        for (QSpinBox *spinBox : window.findChildren<QSpinBox *>())
            if (spinBox->suffix() == " ms")
                spinBox->setValue(1);
    }

    bool ready() const { return editor && run && debug && step && undo && redo && registers; }

    void use(ProcessorType t)
    {
        window.processorChanged(t);
        QApplication::processEvents();
    }

    /// Run to the end. Finished = the window re-enabled Debug Run
    /// (runFinishedSlot, main_window.cpp:999).
    bool runToEnd(const std::string &source)
    {
        editor->setPlainText(QString::fromStdString(source));
        QApplication::processEvents();
        run->trigger();
        return waitUntil([this] { return debug->isEnabled() && run->text() == "Run"; });
    }

    bool stop()
    {
        if (run->text() != "Stop")
            return false;
        run->trigger();
        return waitUntil([this] { return debug->isEnabled(); });
    }

    uint64_t gpr(int index) const
    {
        const QString text =
            registers->data(registers->index(index, 2), Qt::DisplayRole).toString();
        bool ok = false;
        const uint64_t value = text.mid(2).toULongLong(&ok, 16); // "0x..." by default
        return ok ? value : 0xDEADBEEFDEADBEEFull;
    }

    std::vector<uint64_t> allGprs() const
    {
        std::vector<uint64_t> v;
        for (int i = 0; i < 32; ++i)
            v.push_back(gpr(i));
        return v;
    }
};

ProcessorType typeNamed(const std::string &name)
{
    for (ProcessorType t : allTypes())
        if (modelName(t) == name)
            return t;
    return ProcessorType::RVSS;
}

/// The model names, so that ctest's test names read "/h_f" rather than
/// "/4-byte object <04-00 00-00>".
std::vector<std::string> allModelNames()
{
    std::vector<std::string> names;
    for (ProcessorType t : allTypes())
        names.push_back(modelName(t));
    return names;
}

std::string paramName(const ::testing::TestParamInfo<std::string> &info)
{
    return info.param;
}
} // namespace

class GuiRunSmoke : public ::testing::TestWithParam<std::string>
{
};

TEST_P(GuiRunSmoke, EveryProgramRunsToTheEndWithTheRightRegisters)
{
    const ProcessorType type = typeNamed(GetParam());
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(type);

    for (const Program &program : programs())
    {
        SCOPED_TRACE(std::string(program.name) + " on " + modelName(type));
        ASSERT_TRUE(w.runToEnd(padFor(type, program.source))) << "did not finish";
        if (type == ProcessorType::RV5Stage_NH_NF && !program.valuesOnNhNf)
            continue;
        for (const auto &[reg, value] : program.expected)
            EXPECT_EQ(w.gpr(reg), value) << "x" << reg;
    }
    EXPECT_TRUE(w.modals.seen.isEmpty()) << w.modals.seen.join(" | ").toStdString();
}

TEST_P(GuiRunSmoke, StepUndoRedoStopAndSwitch)
{
    const ProcessorType type = typeNamed(GetParam());
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(type);

    // Debug Run the crash program: assemble, load (the profiler mapping
    // that aborted), paused.
    w.editor->setPlainText(QString::fromStdString(padFor(type, programs()[0].source)));
    QApplication::processEvents();
    w.debug->trigger();
    ASSERT_TRUE(waitUntil([&] { return w.step->isEnabled(); }));

    // Three steps: well short of the end on every model (the program is
    // nine instructions), so the step after them is an ordinary one.
    for (int i = 0; i < 3; ++i)
    {
        w.step->trigger();
        QApplication::processEvents();
    }
    spin(50);
    const auto before = w.allGprs();
    w.step->trigger();
    spin(50);
    w.undo->trigger();
    spin(50);
    EXPECT_EQ(w.allGprs(), before) << "Undo did not return the registers to before the step";
    w.redo->trigger();
    spin(50);
    ASSERT_TRUE(w.stop());

    // Switch to every other model and run the crash program to the end.
    for (ProcessorType next : allTypes())
    {
        if (next == type)
            continue;
        SCOPED_TRACE("switched to " + modelName(next));
        w.use(next);
        ASSERT_TRUE(w.runToEnd(padFor(next, programs()[0].source)));
        EXPECT_EQ(w.gpr(a0), 0x7F800000u);
        EXPECT_EQ(w.gpr(a1), 0x08u);
        EXPECT_EQ(w.gpr(a2), 1u);
        EXPECT_EQ(w.gpr(a3), 0x80u);
    }
    EXPECT_TRUE(w.modals.seen.isEmpty()) << w.modals.seen.join(" | ").toStdString();
}

TEST_P(GuiRunSmoke, AnEcallReadWaitsAndStopEndsIt)
{
    const ProcessorType type = typeNamed(GetParam());
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(type);

    w.editor->setPlainText(QString::fromStdString(padFor(type, ".data\n"
                                                               "inbuf: .dword 0\n"
                                                               ".text\n"
                                                               "    li a0, 0\n"
                                                               "    la a1, inbuf\n"
                                                               "    li a2, 4\n"
                                                               "    li a7, 63\n"
                                                               "    ecall\n")));
    QApplication::processEvents();
    w.run->trigger();
    spin(500);
    // No input can be given from the window (known_issues 140), so it waits.
    EXPECT_EQ(w.run->text(), "Stop") << "the read did not wait for input";
    EXPECT_TRUE(w.stop()) << "Stop did not end a run waiting in an ecall read";
    EXPECT_TRUE(w.modals.seen.isEmpty()) << w.modals.seen.join(" | ").toStdString();
}

// ---------------------------------------------------------------------------
// U6 Part B: the end of a program keeps its state (known_issues 142; 67
// extended). Debug Run and Step to the end, then Undo/Redo, Step, Run again;
// Stop in the middle; an ecall exit. Same program on all five models,
// padded; register values by hand:
//   t0 = 5, t1 = t0 + 2 = 7, t2 = t0 + t1 = 12.
namespace
{
const char *kEndProgram = ".text\n"
                          "    addi t0, x0, 5\n"
                          "    nop\n    nop\n    nop\n    nop\n"
                          "    addi t1, t0, 2\n"
                          "    nop\n    nop\n    nop\n    nop\n"
                          "    add t2, t0, t1\n";
// a0 = 3, a7 = 10 (exit); t3 = 9 after the exit must never run.
const char *kExitProgram = ".text\n"
                           "    addi a0, x0, 3\n"
                           "    nop\n    nop\n    nop\n    nop\n"
                           "    addi a7, x0, 10\n"
                           "    nop\n    nop\n    nop\n    nop\n"
                           "    ecall\n"
                           "    addi t3, x0, 9\n";
constexpr int t0r = 5, t1r = 6, t2r = 7, t3r = 28;

QLabel *runStatus(MainWindow &window) { return window.findChild<QLabel *>("runStatusLabel"); }

/// Debug Run, then Step until the window goes idle. Returns the registers
/// before the step that ended it.
std::vector<uint64_t> debugStepToTheEnd(Window &w, const char *source)
{
    w.editor->setPlainText(QString::fromUtf8(source));
    QApplication::processEvents();
    w.debug->trigger();
    EXPECT_TRUE(waitUntil([&] { return w.step->isEnabled(); }));
    std::vector<uint64_t> beforeLast = w.allGprs(), previous = w.allGprs();
    for (int i = 0; i < 300 && w.run->text() == "Stop"; ++i)
    {
        beforeLast = previous;
        w.step->trigger();
        spin(15);
        previous = w.allGprs();
    }
    waitUntil([&] { return w.debug->isEnabled() && w.run->text() == "Run"; });
    return beforeLast;
}
} // namespace

TEST_P(GuiRunSmoke, SteppedToTheEndTheWindowKeepsTheFinalState)
{
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(typeNamed(GetParam()));

    const auto beforeLast = debugStepToTheEnd(w, kEndProgram);
    ASSERT_EQ(w.run->text(), "Run") << "the stepped program never ended";
    EXPECT_EQ(w.gpr(t0r), 5u);
    EXPECT_EQ(w.gpr(t1r), 7u);
    EXPECT_EQ(w.gpr(t2r), 12u) << "the final state was wiped on the step that produced it";
    QLabel *status = runStatus(w.window);
    ASSERT_NE(status, nullptr) << "no status label named runStatusLabel";
    EXPECT_EQ(status->text(), "Program finished");
    EXPECT_FALSE(w.step->isEnabled()) << "Step is still offered after the end";

    const auto atEnd = w.allGprs();
    ASSERT_TRUE(w.undo->isEnabled());
    w.undo->trigger();
    spin(50);
    EXPECT_EQ(w.allGprs(), beforeLast) << "Undo after the end";
    w.redo->trigger();
    spin(50);
    EXPECT_EQ(w.allGprs(), atEnd) << "Redo after the Undo";

    // Run again from the end: a fresh run, the same result.
    w.run->trigger();
    ASSERT_TRUE(waitUntil([&] { return w.debug->isEnabled() && w.run->text() == "Run"; }));
    EXPECT_EQ(w.gpr(t2r), 12u);
    EXPECT_EQ(status->text(), "Program finished");
    EXPECT_TRUE(w.modals.seen.isEmpty()) << w.modals.seen.join(" | ").toStdString();
}

TEST_P(GuiRunSmoke, AStoppedRunIsNotAFinishedOne)
{
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(typeNamed(GetParam()));
    w.editor->setPlainText(QString::fromUtf8(kEndProgram));
    QApplication::processEvents();
    w.debug->trigger();
    ASSERT_TRUE(waitUntil([&] { return w.step->isEnabled(); }));
    for (int i = 0; i < 3; ++i)
    {
        w.step->trigger();
        spin(15);
    }
    ASSERT_TRUE(w.stop()) << "Stop in the middle did not return the window to idle";
    QLabel *status = runStatus(w.window);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), "Stopped");
}

TEST_P(GuiRunSmoke, ASteppedExitKeepsItsStateToo)
{
    Window w;
    ASSERT_TRUE(w.ready());
    w.use(typeNamed(GetParam()));
    debugStepToTheEnd(w, kExitProgram);
    ASSERT_EQ(w.run->text(), "Run");
    EXPECT_EQ(w.gpr(10), 3u);
    EXPECT_EQ(w.gpr(17), 10u);
    EXPECT_EQ(w.gpr(t3r), 0u) << "an instruction after the exit ran";
    QLabel *status = runStatus(w.window);
    ASSERT_NE(status, nullptr);
    EXPECT_EQ(status->text(), "Program finished");
}

INSTANTIATE_TEST_SUITE_P(AllModels, GuiRunSmoke, ::testing::ValuesIn(allModelNames()), paramName);
