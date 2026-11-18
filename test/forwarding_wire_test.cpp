/**
 * @file forwarding_wire_test.cpp
 * @brief The circuit view must not light a forwarding path that is not forwarding.
 *
 * known_issues 81. `SetActiveWireNames` decided whether to light the
 * forwarding wires with the bare test `reg_write && rd != 0`
 * (`rv5s_processor_h_f.cpp:128, 145`, `rv5s_processor_nh_f.cpp:121,
 * 140`) - the same test the datapath used before 79 replaced it. The
 * control unit sets `reg_write` for every OP-FP instruction and the
 * EX/MEM `rd` field carries the same five bits as `frd`, so
 * `fadd.d f5, …` followed by anything reading x5 lit the integer
 * forwarding path, although after 79 nothing is forwarded along it. The
 * diagram claimed a mechanism the machine was not using.
 *
 * No Spy subclass is needed: `active_wires_` (`processor_base.h:193`)
 * and `SetActiveWireNames()` (`:288`) are both public members of
 * `ProcessorBase` - the class declares `public:` at `:85`, `private:` at
 * `:278` and `public:` again at `:287`. `StepAndNotify()` calls
 * `SetActiveWireNames()` on every cycle (`rv5s_processor_base.cpp:69`),
 * so stepping and reading the list is the whole fixture.
 *
 * The three wires asserted on are `FDU_to_FMUX1_cntrl`, `FDU_to_fMUX2`
 * and `FDU_to_FMUX3`. They are the only forwarding wires that are
 * CONDITIONAL: `P2_to_FDU_rs1`, `P2_to_FDU_RS2` and `P4EB_to_FWD_unit`
 * sit in the constructor's always-active backbone (`h_f.cpp:68-70`), so
 * asserting on those would pass whatever the guard said.
 *
 * Each case is checked as a union over every cycle of the program, so a
 * wire lit on any cycle counts. The programs are two or three
 * instructions long and the only producer with `rd != 0` is the one
 * named, so there is nowhere else for a lit wire to come from.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <QSet>
#include <QString>
#include <QTemporaryDir>

#include <fstream>
#include <string>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace
{
/// The conditional forwarding wires, i.e. the ones the guards decide.
/// The EX/MEM arm lights all three; the MEM/WB arm lights only the first
/// and the third, because its rs1 branch omits FDU_to_fMUX2
/// (the MEM/WB arm of SetActiveWireNames).
const char *const kConditional[] = {"FDU_to_FMUX1_cntrl", "FDU_to_fMUX2", "FDU_to_FMUX3"};
const char *const kExMemArm[]    = {"FDU_to_FMUX1_cntrl", "FDU_to_fMUX2", "FDU_to_FMUX3"};
const char *const kMemWbArm[]    = {"FDU_to_FMUX1_cntrl", "FDU_to_FMUX3"};

/// Backbone wires, listed only so the test's own reasoning is checkable:
/// these are on from construction and must be on in every case below.
const char *const kAlwaysOn[] = {"P2_to_FDU_rs1", "P2_to_FDU_RS2", "P4EB_to_FWD_unit"};

int serial = 0;

std::unique_ptr<ProcessorBase> load(Model model, const QTemporaryDir &dir,
                                    const std::string &body)
{
    const std::string path =
        dir.path().toStdString() + "/fw" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    auto vm = makeProcessor(model);
    vm->LoadProgram(assemble(path));
    return vm;
}

/// Every wire name lit on any cycle of the program.
QSet<QString> litDuring(Model model, const QTemporaryDir &dir, const std::string &body)
{
    auto          vm = load(model, dir, body);
    QSet<QString> seen;
    // Generous: the longest program here is three instructions, which
    // drains in eight cycles on the slowest of the two models.
    for (int cycle = 0; cycle < 24; ++cycle)
    {
        const bool more = vm->StepAndNotify();
        for (const QString &wire : vm->active_wires_)
            seen.insert(wire);
        if (!more)
            break;
    }
    return seen;
}

std::string listOf(const QSet<QString> &set, const char *const *names, int count)
{
    std::string out;
    for (int i = 0; i < count; ++i)
        if (set.contains(QString::fromLatin1(names[i])))
            out += (out.empty() ? "" : ", ") + std::string(names[i]);
    return out.empty() ? "(none)" : out;
}

void expectConditionalWiresDark(Model model, const QSet<QString> &lit, const std::string &what)
{
    for (const char *wire : kConditional)
        EXPECT_FALSE(lit.contains(QString::fromLatin1(wire)))
            << name(model) << ", " << what << ": " << wire
            << " is lit, but nothing is forwarded along it. Lit of the three: "
            << listOf(lit, kConditional, 3);
}

void expectWiresLit(Model model, const QSet<QString> &lit, const char *const *wires, int count,
                    const std::string &what)
{
    for (int i = 0; i < count; ++i)
        EXPECT_TRUE(lit.contains(QString::fromLatin1(wires[i])))
            << name(model) << ", " << what << ": " << wires[i]
            << " is dark, but this producer does reach that consumer. Lit of the three: "
            << listOf(lit, kConditional, 3);
}

/// Both models with a forwarding unit. h_nf and nh_nf have none of these
/// wires at all.
const std::vector<Model> &forwardingModels()
{
    static const std::vector<Model> models{Model::Rv5HF, Model::Rv5NhF};
    return models;
}

// ---- the programs -------------------------------------------------------
// t0 is x5, so an f5 producer and an x5 consumer collide on the index.
// The consumer reads t0 twice, so one program exercises both the rs1 and
// the rs2 branch of each guarded block.
//
// The two distances reach the two guards, and they are the two distances
// at which the machine really forwards:
//
//   adjacent  -> the EX/MEM guard
//   one nop   -> the MEM/WB guard
//
// Until known_issues 95 was fixed the MEM/WB guard read mem_wb_reg_.prev_*
// and fired one nop LATER, at two nops, where nothing is forwarded. The
// three tests below used two nops to reach it then; they use one now.
// TheMemWbGuardFiresExactlyWhenTheMachineForwards pins all four distances.

const char *kGapNone = "";
const char *kGapOne  = "    nop\n";
const char *kGapTwo  = "    nop\n    nop\n";
const char *kGapThree = "    nop\n    nop\n    nop\n";

std::string program(const char *producer, const char *gap)
{
    return std::string(producer) + gap + "    add s0, t0, t0\n";
}

const char *kIntProducer = "    addi t0, x0, 7\n";
const char *kFpProducer  = "    fadd.d f5, f1, f2\n";
// fcvt.l.d writes the INTEGER register t0 (funct7 1100001), so this one
// must keep lighting the wires: the fix must not over-reach from "it is
// an F instruction" to "it cannot reach an integer consumer".
const char *kFpToGprProducer = "    fcvt.l.d t0, f6\n";
} // namespace

TEST(ForwardingWires, AnFpProducerDoesNotLightTheIntegerForwardingPath)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : forwardingModels())
    {
        expectConditionalWiresDark(model, litDuring(model, dir, program(kFpProducer, kGapNone)),
                                   "fadd.d f5 then a consumer reading t0 (x5), adjacent");
        expectConditionalWiresDark(model, litDuring(model, dir, program(kFpProducer, kGapOne)),
                                   "fadd.d f5 then a consumer reading t0, one nop apart");
    }
}

TEST(ForwardingWires, AnIntegerProducerStillLightsIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : forwardingModels())
    {
        expectWiresLit(model, litDuring(model, dir, program(kIntProducer, kGapNone)), kExMemArm, 3,
                       "addi t0 then a consumer reading t0, adjacent");
        expectWiresLit(model, litDuring(model, dir, program(kIntProducer, kGapOne)), kMemWbArm, 2,
                       "addi t0 then a consumer reading t0, one nop apart");
    }
}

TEST(ForwardingWires, AnFpInstructionThatWritesAGprStillLightsIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : forwardingModels())
    {
        expectWiresLit(model, litDuring(model, dir, program(kFpToGprProducer, kGapNone)),
                       kExMemArm, 3, "fcvt.l.d t0 then a consumer reading t0, adjacent");
        expectWiresLit(model, litDuring(model, dir, program(kFpToGprProducer, kGapOne)),
                       kMemWbArm, 2, "fcvt.l.d t0 then a consumer reading t0, one nop apart");
    }
}

TEST(ForwardingWires, TheMemWbGuardFiresExactlyWhenTheMachineForwards)
{
    // known_issues 95. SetActiveWireNames runs AFTER the whole Step, when
    // id_ex_reg_ already holds the instruction that executes next. Its
    // MEM/WB guard used to read mem_wb_reg_.prev_*, which is correct DURING
    // execute and one generation stale here, so the MEM/WB wires were dark
    // one nop apart - where the machine forwards from MEM/WB - and lit two
    // nops apart, where the value is already in the register file.
    //
    // The four distances, producer `addi t0` and consumer reading t0:
    //   0 nops  forwarded from EX/MEM -> the EX/MEM arm's three wires
    //   1 nop   forwarded from MEM/WB -> the MEM/WB arm's two wires
    //   2 nops  read from the register file (written back first) -> dark
    //   3 nops  dark
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : forwardingModels())
    {
        expectWiresLit(model, litDuring(model, dir, program(kIntProducer, kGapNone)), kExMemArm, 3,
                       "0 nops apart");
        expectWiresLit(model, litDuring(model, dir, program(kIntProducer, kGapOne)), kMemWbArm, 2,
                       "1 nop apart");
        expectConditionalWiresDark(model, litDuring(model, dir, program(kIntProducer, kGapTwo)),
                                   "2 nops apart");
        expectConditionalWiresDark(model, litDuring(model, dir, program(kIntProducer, kGapThree)),
                                   "3 nops apart");
    }
}

TEST(ForwardingWires, TheBackboneWiresAreOnWhateverTheGuardDecides)
{
    // The reason the three asserted wires are the three asserted wires.
    // If these ever became conditional, the tests above would start
    // passing for the wrong reason and this says so first.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    for (Model model : forwardingModels())
        for (const char *producer : {kFpProducer, kIntProducer})
        {
            const QSet<QString> lit = litDuring(model, dir, program(producer, kGapNone));
            for (const char *wire : kAlwaysOn)
                EXPECT_TRUE(lit.contains(QString::fromLatin1(wire)))
                    << name(model) << ": " << wire
                    << " is not always on any more; the conditional set must be rechecked";
        }
}
