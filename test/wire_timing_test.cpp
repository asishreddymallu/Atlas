/**
 * @file wire_timing_test.cpp
 * @brief After each Step the circuit view draws the cycle the stage labels
 *        show (known_issues 95).
 *
 * The semantic: after a Step, `setProcessorState` labels the instruction
 * in `if_id_reg_` as ID, `id_ex_reg_` as EX, `ex_mem_reg_` as MEM and
 * `mem_wb_reg_` as WB. A conditional wire belonging to stage S is lit if
 * and only if the instruction labelled S uses it; the always-on backbone
 * is unchanged.
 *
 * Two kinds of test.
 *
 * TRACES. One instruction alone in a stream of nops on h_f, stepped one
 * cycle at a time. Where it sits each cycle is read from the labels
 * themselves (`processor_state_.programCounters`), and the expected set
 * of lit conditional wires is built from a per-stage rule table written
 * out below and a per-instruction flag table written out literally.
 *
 * What that oracle can and cannot prove: the rule table is the audit's
 * code reading written down a second time, so a trace passing proves the
 * diagram follows that reading - each wire tracks the register that holds
 * the labelled instruction of its stage - for these seven instructions on
 * this one model. It cannot prove that the reading is the textbook's,
 * that a wire's name matches the line it is drawn as, or that the
 * datapath computes the right thing. Those need a person and the
 * drawing; the rendered frames are for that.
 *
 * CROSS-CHECKS. These do not share the oracle's reasoning: they compare
 * what the diagram claims after step k with what the machine then does in
 * step k+1, on every pipelined model and several programs:
 *   - the hazard-unit wires are lit  <=>  step k+1 stalls (the PC holds);
 *   - ANDGATE_to_PCMUX is lit        <=>  step k+1 counts a misprediction;
 *   - ALU1_to_PCMuxUp is lit         <=>  step k+1 does not redirect;
 *   - the MEM/WB register the forwarding wires read after step k is the
 *     prev_* copy EX forwards from during step k+1.
 * They prove the predictions match the machine, not that the machine is
 * right.
 */
#include "float_test_support.h"
#include "wire_trace_support.h"

#include <gtest/gtest.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <fstream>
#include <map>
#include <string>
#include <vector>

using namespace Atlas;
using namespace Atlas::test_support;
using namespace Atlas::test_support::wire_trace;

namespace
{
int serial = 0;

std::string writeProgram(const QTemporaryDir &dir, const std::string &body)
{
    const std::string path = dir.path().toStdString() + "/wt" + std::to_string(serial++) + ".s";
    std::ofstream     file(path);
    file << ".text\n" << body;
    return path;
}

/// Exposes the pipeline registers, read only, for the MEM/WB cross-check.
template <class P> struct Probe : P
{
    using P::ex_mem_reg_;
    using P::id_ex_reg_;
    using P::if_id_reg_;
    using P::mem_wb_reg_;
};

std::unique_ptr<ProcessorBase> makeProbe(Model model)
{
    switch (model)
    {
    case Model::Rv5NhNf: return std::make_unique<Probe<RV5StageProcessorNHNF>>();
    case Model::Rv5NhF:  return std::make_unique<Probe<RV5StageProcessorNHF>>();
    case Model::Rv5HNf:  return std::make_unique<Probe<RV5StageProcessorHNF>>();
    case Model::Rv5HF:   return std::make_unique<Probe<RV5StageProcessorHF>>();
    case Model::Rvss:    break;
    }
    return nullptr;
}

const MEM_WB_Register &memWb(Model model, ProcessorBase &vm)
{
    switch (model)
    {
    case Model::Rv5NhNf: return static_cast<Probe<RV5StageProcessorNHNF> &>(vm).mem_wb_reg_;
    case Model::Rv5NhF:  return static_cast<Probe<RV5StageProcessorNHF> &>(vm).mem_wb_reg_;
    case Model::Rv5HNf:  return static_cast<Probe<RV5StageProcessorHNF> &>(vm).mem_wb_reg_;
    default:             return static_cast<Probe<RV5StageProcessorHF> &>(vm).mem_wb_reg_;
    }
}

QSet<QString> conditionalLit(const ProcessorBase &vm, const QSet<QString> &backbone)
{
    QSet<QString> lit;
    for (const QString &w : vm.active_wires_)
        if (!backbone.contains(w))
            lit.insert(w);
    return lit;
}

std::string sorted(const QSet<QString> &set)
{
    QStringList list(set.begin(), set.end());
    list.sort();
    return list.isEmpty() ? "(none)" : list.join(' ').toStdString();
}

// ---- the oracle -----------------------------------------------------------

} // namespace

TEST(WireTiming, EachInstructionLightsItsStagesWiresInTheCycleItIsLabelledThere)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (const Traced &t : traced())
    {
        setupVmStateDirectory();
        auto vm = makeProcessor(Model::Rv5HF);
        const QSet<QString> backbone(vm->active_wires_.begin(), vm->active_wires_.end());
        vm->LoadProgram(assemble(writeProgram(dir, kLead + t.body)));

        int stagesSeen = 0;
        for (int cycle = 1; cycle <= 16; ++cycle)
        {
            const bool more = vm->StepAndNotify();

            const auto     &pcs = vm->processor_state_.programCounters;
            std::map<Stage, bool> in;
            QSet<QString>         expected;
            for (Stage s : {IF, ID, EX, MEM, WB})
            {
                in[s] = pcs[s] == kTracedPc;
                if (in[s])
                {
                    expected += hfWiresFor(s, t.flags);
                    ++stagesSeen;
                }
            }
            if (pcPlusFourSelected(in, t.flags))
                expected << "ALU1_to_PCMuxUp";

            EXPECT_EQ(sorted(conditionalLit(*vm, backbone)), sorted(expected))
                << t.what << ", cycle " << cycle << " (IF " << pcs[IF] << " ID " << pcs[ID]
                << " EX " << pcs[EX] << " MEM " << pcs[MEM] << " WB " << pcs[WB] << ")";
            if (!more)
                break;
        }
        // The trace must actually have walked the instruction through all
        // five labels, or it proves nothing.
        EXPECT_EQ(stagesSeen, 5) << t.what;
    }
}

namespace
{
/// Programs for the cross-checks: load-use, back-to-back dependencies
/// (h_nf's two-cycle stall), FP dependencies, a taken backward branch, a
/// call and a return.
std::vector<std::string> crossCheckPrograms()
{
    return {
        "    addi t0, x0, 90\n    sd t0, 256(x0)\n    ld t1, 256(x0)\n    add t2, t1, t1\n"
        "    add t3, t2, t1\n    sub t4, t3, t2\n",
        "    li t0, 3\n    fcvt.d.l f1, t0\n    fmadd.d f4, f1, f1, f1\n    fadd.d f5, f4, f1\n",
        "    addi t0, x0, 3\nloop:\n    addi t0, t0, -1\n    bne t0, x0, loop\n    addi t1, x0, 7\n",
        // Padded so that no jump target is the PC or PC+4 of the cycle
        // that redirects to it: the redirect detector below tells a
        // redirect from a fall-through by the PC alone.
        "    jal ra, f\n    addi t1, x0, 1\n    beq x0, x0, end\n" + kTail + "f:\n"
        "    addi t0, x0, 5\n    jalr x0, 0(ra)\n" + kTail + "end:\n    addi t2, x0, 2\n",
    };
}

const std::vector<Model> &pipelinedModels()
{
    static const std::vector<Model> m{Model::Rv5NhNf, Model::Rv5NhF, Model::Rv5HNf,
                                      Model::Rv5HF};
    return m;
}

struct Snapshot
{
    QSet<QString> lit;
    uint64_t      pc;
    unsigned      mispredictions;
    uint32_t      wbInstruction;
    bool          wbRegWrite;
    uint8_t       wbRd;
};

Snapshot snap(Model model, ProcessorBase &vm)
{
    const MEM_WB_Register &wb = memWb(model, vm);
    return {QSet<QString>(vm.active_wires_.begin(), vm.active_wires_.end()),
            vm.program_counter_,
            vm.branch_mispredictions_,
            wb.instruction,
            wb.reg_write,
            wb.rd};
}
} // namespace

TEST(WireTiming, TheDiagramPredictsWhatTheNextStepDoes)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    int stalls = 0, redirects = 0, mispredictions = 0, checked = 0;
    for (Model model : pipelinedModels())
    {
        const bool hasHdu = model == Model::Rv5HF || model == Model::Rv5HNf;
        for (const std::string &body : crossCheckPrograms())
        {
            setupVmStateDirectory();
            auto vm = makeProbe(model);
            vm->LoadProgram(assemble(writeProgram(dir, body)));

            vm->StepAndNotify();
            Snapshot before = snap(model, *vm);
            for (int cycle = 2; cycle <= 40; ++cycle)
            {
                const bool     more  = vm->StepAndNotify();
                const Snapshot after = snap(model, *vm);
                const std::string where =
                    std::string(name(model)) + ", step " + std::to_string(cycle) + " of\n" + body;

                const bool stalled    = after.pc == before.pc;
                const bool redirected = after.pc != before.pc && after.pc != before.pc + 4;
                stalls += stalled;
                redirects += redirected;

                if (hasHdu)
                    EXPECT_EQ(before.lit.contains("HDU_to_P1"), stalled) << where;
                else
                    EXPECT_FALSE(stalled) << where << "\n(a model with no hazard unit held its PC)";

                const bool counted = after.mispredictions == before.mispredictions + 1;
                mispredictions += counted;
                EXPECT_EQ(before.lit.contains("ANDGATE_to_PCMUX"), counted) << where;

                EXPECT_EQ(before.lit.contains("ALU1_to_PCMuxUp"), !redirected) << where;

                const MEM_WB_Register &wb = memWb(model, *vm);
                EXPECT_EQ(wb.prev_instruction, before.wbInstruction) << where;
                EXPECT_EQ(wb.prev_reg_write, before.wbRegWrite) << where;
                EXPECT_EQ(wb.prev_rd, before.wbRd) << where;

                ++checked;
                before = after;
                if (!more)
                    break;
            }
        }
    }
    // The comparisons are only worth anything if each kind of event
    // actually happened somewhere in the sweep.
    EXPECT_GT(stalls, 0);
    EXPECT_GT(redirects, 0);
    EXPECT_GT(mispredictions, 0);
    EXPECT_GT(checked, 100);
}
