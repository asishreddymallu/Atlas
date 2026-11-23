/**
 * @file rvss_wire_list_test.cpp
 * @brief Every path that redraws the single-cycle diagram starts from the
 *        backbone (known_issues 96).
 *
 * `RVSSProcessor::SetActiveWireNames` used to append without clearing, so
 * any caller that did not erase afterwards - DebugRun, Undo, Redo - left
 * the previous cycle's wires on the list, and the list grew with every
 * press. One test per path that emits `updateCircuitStateSignal`.
 *
 * What is asserted is the emitted list, which is what `CircuitScene`
 * draws: no name twice (a repeated press re-appending the same names is
 * what growth looks like) and, where the control signals are fresh, no
 * name from an earlier instruction.
 *
 * Undo and Redo are checked for growth only. They do not restore the
 * control unit, so after an Undo the conditional wires describe the last
 * instruction DECODED rather than the state undone to - a separate
 * defect with its own known_issues entry, not something this test pins.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <QList>
#include <QSet>
#include <QString>
#include <QTemporaryDir>

#include <fstream>
#include <string>
#include <vector>

using namespace Atlas;
using namespace Atlas::test_support;

namespace
{
struct Recorder
{
    std::vector<QList<QString>> lists;
};

std::unique_ptr<ProcessorBase> loaded(const QTemporaryDir &dir, const std::string &body,
                                      Recorder &recorder)
{
    static int serial = 0;
    const std::string path =
        dir.path().toStdString() + "/rw" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    auto vm = makeProcessor(Model::Rvss);
    vm->LoadProgram(assemble(path));
    QObject::connect(vm.get(), &ProcessorBase::updateCircuitStateSignal,
                     [&recorder](const QList<QString> &wires) { recorder.lists.push_back(wires); });
    return vm;
}

std::string joined(const QList<QString> &list)
{
    return QStringList(list).join(' ').toStdString();
}

/// The names that appear more than once, space-separated; empty if none.
std::string duplicatesIn(const QList<QString> &list)
{
    QSet<QString> seen;
    QStringList   twice;
    for (const QString &w : list)
    {
        if (seen.contains(w) && !twice.contains(w))
            twice << w;
        seen.insert(w);
    }
    return joined(twice);
}

// Three identical instructions: every cycle appends the same conditional
// names, so a list that is not truncated shows them twice.
const char *kSame = "    addi t0, x0, 1\n    addi t1, x0, 2\n    addi t2, x0, 3\n";

// A load and then an ALU op: the load's memory wires must not survive
// into the next cycle.
const char *kLoadThenAlu = "    lw t0, 0(x0)\n    addi t1, x0, 2\n";
} // namespace

TEST(RvssWireList, StepAndNotifyStartsFromTheBackboneAfterAnUndo)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Recorder recorder;
    auto vm = loaded(dir, kSame, recorder);

    vm->StepAndNotify();
    vm->StepAndNotify();
    vm->Undo(); // leaves the list as Undo built it
    vm->StepAndNotify();

    ASSERT_EQ(recorder.lists.size(), 4u);
    EXPECT_EQ(duplicatesIn(recorder.lists.back()), "")
        << "the Step after an Undo re-appended names already on the list";
    EXPECT_EQ(joined(recorder.lists.back()), joined(recorder.lists[1]))
        << "the same instruction stepped twice must draw the same wires";
}

TEST(RvssWireList, DebugRunDrawsOnlyTheCurrentInstruction)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Recorder recorder;
    auto vm = loaded(dir, kLoadThenAlu, recorder);

    vm->DebugRun();

    ASSERT_EQ(recorder.lists.size(), 2u);
    EXPECT_TRUE(recorder.lists[0].contains("Control_to_MemRead")) << "the load's own cycle";
    EXPECT_FALSE(recorder.lists[1].contains("Control_to_MemRead"))
        << "the addi's cycle still shows the load's MemRead";
    EXPECT_EQ(duplicatesIn(recorder.lists[1]), "");
}

TEST(RvssWireList, UndoDoesNotGrowTheList)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Recorder recorder;
    auto vm = loaded(dir, kSame, recorder);

    vm->StepAndNotify();
    vm->StepAndNotify();
    vm->StepAndNotify();
    vm->Undo();
    vm->Undo();

    ASSERT_EQ(recorder.lists.size(), 5u);
    EXPECT_EQ(duplicatesIn(recorder.lists[4]), "");
    EXPECT_EQ(recorder.lists[4].size(), recorder.lists[3].size())
        << "two Undos in a row drew lists of different lengths";
}

TEST(RvssWireList, RedoDoesNotGrowTheList)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    Recorder recorder;
    auto vm = loaded(dir, kSame, recorder);

    vm->StepAndNotify();
    vm->StepAndNotify();
    vm->StepAndNotify();
    vm->Undo();
    vm->Undo();
    vm->Redo();
    vm->Redo();

    ASSERT_EQ(recorder.lists.size(), 7u);
    EXPECT_EQ(duplicatesIn(recorder.lists[6]), "");
    EXPECT_EQ(recorder.lists[6].size(), recorder.lists[5].size())
        << "two Redos in a row drew lists of different lengths";
}
