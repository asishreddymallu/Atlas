/**
 * @file stage_tag_test.cpp
 * @brief Every lit conditional wire carries the stage it is drawn for.
 *
 * `lightWire(name, stage)` records, next to each name in `active_wires_`,
 * the stage whose instruction the wire is lit for, in
 * `active_wire_stages_` (a bitmask, one bit per stage). The circuit view
 * uses it to emphasise one stage's wires and dim the rest.
 *
 * The oracle (wire_trace_support.h) is known_issues 95's audit table: a
 * wire is tagged with the stage column of that table - the stage of the
 * register its condition reads (ID = if_id_reg_, EX = id_ex_reg_, MEM =
 * ex_mem_reg_, WB = mem_wb_reg_). Two groups read more than one register
 * and are tagged with the stage they are drawn in: the forwarding wires
 * (EX, the consumer's stage; they also read EX/MEM or MEM/WB) and the PC+4
 * wire ALU1_to_PCMuxUp (IF; its condition reads EX/MEM and ID/EX to rule
 * out a redirect).
 *
 * What this cannot prove: the oracle and the tags come from the same
 * reading of the code, so passing shows the tags follow that reading for
 * these instructions on h_f. Whether a textbook would put the wire in that
 * stage, or whether the name is the line it is drawn as, needs a person
 * and the drawing.
 */
#include "float_test_support.h"
#include "wire_trace_support.h"

#include <gtest/gtest.h>

#include <QStringList>
#include <QTemporaryDir>

#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;
using namespace Kites::test_support::wire_trace;

namespace
{
int serial = 0;

std::string writeProgram(const QTemporaryDir &dir, const std::string &body)
{
    const std::string path = dir.path().toStdString() + "/st" + std::to_string(serial++) + ".s";
    std::ofstream     file(path);
    file << ".text\n" << body;
    return path;
}

const char *kStageName[] = {"IF", "ID", "EX", "MEM", "WB"};

std::string maskText(quint8 mask)
{
    std::string out;
    for (int s = 0; s < 5; ++s)
        if (mask & (1u << s))
            out += (out.empty() ? "" : "+") + std::string(kStageName[s]);
    return out.empty() ? "-" : out;
}

/// "name:STAGE" for every entry past the backbone, sorted; the backbone
/// entries are checked separately to carry no stage.
std::string staged(const ProcessorBase &vm)
{
    QStringList out;
    for (qsizetype i = qsizetype(vm.always_active_wires_count_); i < vm.active_wires_.size(); ++i)
        out << vm.active_wires_[i] + ":" + QString::fromStdString(maskText(vm.active_wire_stages_[i]));
    out.sort();
    return out.join(' ').toStdString();
}

std::string expectedStaged(const std::map<QString, quint8> &want)
{
    QStringList out;
    for (const auto &[name, mask] : want)
        out << name + ":" + QString::fromStdString(maskText(mask));
    out.sort();
    return out.join(' ').toStdString();
}

/// The whole list, names and stages in order - what Undo/Redo must restore.
std::string fullList(const ProcessorBase &vm)
{
    std::string out;
    for (qsizetype i = 0; i < vm.active_wires_.size(); ++i)
        out += vm.active_wires_[i].toStdString() + ":" + maskText(vm.active_wire_stages_[i]) + " ";
    return out;
}
} // namespace

TEST(StageTags, EachLitWireCarriesTheStageOfTheInstructionItIsDrawnFor)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (const Traced &t : traced())
    {
        setupVmStateDirectory();
        auto vm = makeProcessor(Model::Rv5HF);
        vm->LoadProgram(assemble(writeProgram(dir, kLead + t.body)));

        int stagesSeen = 0;
        for (int cycle = 1; cycle <= 16; ++cycle)
        {
            const bool more = vm->StepAndNotify();
            ASSERT_EQ(vm->active_wire_stages_.size(), vm->active_wires_.size())
                << t.what << ", cycle " << cycle;
            for (qsizetype i = 0; i < qsizetype(vm->always_active_wires_count_); ++i)
                EXPECT_EQ(vm->active_wire_stages_[i], kNoStage)
                    << t.what << ": backbone wire " << vm->active_wires_[i].toStdString()
                    << " carries a stage";

            const auto            &pcs = vm->processor_state_.programCounters;
            std::map<Stage, bool>  in;
            std::map<QString, quint8> want;
            for (Stage s : {IF, ID, EX, MEM, WB})
            {
                in[s] = pcs[s] == kTracedPc;
                if (!in[s])
                    continue;
                ++stagesSeen;
                for (const QString &w : hfWiresFor(s, t.flags))
                    want[w] |= quint8(1u << s);
            }
            if (pcPlusFourSelected(in, t.flags))
                want["ALU1_to_PCMuxUp"] |= stageBit(WireStage::IF);

            EXPECT_EQ(staged(*vm), expectedStaged(want))
                << t.what << ", cycle " << cycle << " (IF " << pcs[IF] << " ID " << pcs[ID]
                << " EX " << pcs[EX] << " MEM " << pcs[MEM] << " WB " << pcs[WB] << ")";
            if (!more)
                break;
        }
        EXPECT_EQ(stagesSeen, 5) << t.what;
    }
}

TEST(StageTags, UndoThenRedoRestoresTheSameStagedList)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // A load-use pair (h models stall), a taken branch (flush) and a
    // forward: something in every stage, on every pipelined model.
    const std::string body = "    addi t0, x0, 9\n    sd t0, 256(x0)\n    ld t1, 256(x0)\n"
                             "    add t2, t1, t1\n    beq x0, x0, out\n    addi t3, x0, 1\n"
                             "out:\n    add t4, t2, t0\n";

    for (Model model : {Model::Rv5NhNf, Model::Rv5NhF, Model::Rv5HNf, Model::Rv5HF})
    {
        setupVmStateDirectory();
        auto vm = makeProcessor(model);
        vm->LoadProgram(assemble(writeProgram(dir, body)));

        std::vector<std::string> after; // after[k] = the list after step k+1
        for (int k = 0; k < 14; ++k)
        {
            const bool more = vm->StepAndNotify();
            after.push_back(fullList(*vm));
            if (!more)
                break;
        }
        ASSERT_GE(after.size(), 6u) << name(model);
        // Restoring an all-unstaged list would pass trivially, so the
        // recorded lists must carry stages for this to mean anything.
        bool anyStaged = false;
        for (const std::string &list : after)
            anyStaged = anyStaged || list.find(":ID") != std::string::npos ||
                        list.find(":EX") != std::string::npos ||
                        list.find(":MEM") != std::string::npos ||
                        list.find(":WB") != std::string::npos;
        EXPECT_TRUE(anyStaged) << name(model) << ": no wire carried a stage in any cycle";

        // Walk back to the first step, then forward to the last.
        for (size_t k = after.size() - 1; k >= 1; --k)
        {
            vm->Undo();
            EXPECT_EQ(fullList(*vm), after[k - 1]) << name(model) << ": Undo to step " << k;
        }
        for (size_t k = 1; k < after.size(); ++k)
        {
            vm->Redo();
            EXPECT_EQ(fullList(*vm), after[k]) << name(model) << ": Redo to step " << k + 1;
        }
    }
}
