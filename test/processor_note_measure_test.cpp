/**
 * @file processor_note_measure_test.cpp
 * @brief The processor chooser's note is checked against the machine: the
 *        nop counts it states are measured here, on every run of the suite.
 *
 * For each pipeline (nh_nf, nh_f, h_nf, h_f) and each kind of producer, a
 * dependent pair is run with 0..4 nops between producer and consumer. The
 * smallest padding from which the consumer reads the right value (and keeps
 * reading it for every larger padding) is that model's requirement:
 *
 *   ALU  : addi t0, x0, 7     ->  addi t1, t0, 1       (t1 = 8)
 *   load : ld t0, 0(t2)       ->  addi t1, t0, 1       (t1 = 8; the word is 7)
 *   FP   : fcvt.d.l f1, t3    ->  fcvt.l.d t1, f1      (t1 = 7)
 *
 * The expected values are arithmetic, not a run of rvss. Every setup
 * instruction is followed by eight nops, so only the pair under test can
 * be short of padding.
 *
 * It also runs the two pseudos that expand into a dependent chain the
 * source cannot pad, unpadded: li of a large constant (lui + addiw) and
 * la of a .data label (auipc + addi). li must give 0x12345678. la must give
 * 0x10000000: the data section starts there (config.h:37) and the label is
 * its first word.
 *
 * ProcessorNoteMeasured.TheNoteStatesTheMeasuredTable then checks
 * processorNote() against the table: the nop counts it names, and whether
 * it says li/la work. So the note cannot drift from the machine.
 */
#include "assembler/assembler.h"
#include "processor/processor_choice.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "ui/main_window/main_window.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <iostream>
#include <fstream>
#include <string>

using namespace Kites;

namespace
{
enum Producer
{
    Alu,
    Load,
    Fp,
    Producers
};
const char *kProducerName[Producers] = {"ALU", "load", "FP"};

struct Model
{
    const char   *name;
    ProcessorType type;
    bool          hazard;
    bool          forwarding;
};
const std::array<Model, 4> kModels = {{
    {"nh_nf", ProcessorType::RV5Stage_NH_NF, false, false},
    {"nh_f", ProcessorType::RV5Stage_NH_F, false, true},
    {"h_nf", ProcessorType::RV5Stage_H_NF, true, false},
    {"h_f", ProcessorType::RV5Stage_H_F, true, true},
}};

constexpr int kMaxPad = 4;

std::string nops(int n)
{
    std::string s;
    for (int i = 0; i < n; ++i)
        s += "    nop\n";
    return s;
}

std::string pairProgram(Producer p, int pad)
{
    switch (p)
    {
    case Alu:
        return ".text\n    addi t0, x0, 7\n" + nops(pad) + "    addi t1, t0, 1\n";
    case Load:
        return ".data\nV: .dword 7\n.text\n    lui t2, 0x10000\n" + nops(8) +
               "    ld t0, 0(t2)\n" + nops(pad) + "    addi t1, t0, 1\n";
    case Fp:
        return ".text\n    addi t3, x0, 7\n" + nops(8) + "    fcvt.d.l f1, t3\n" + nops(pad) +
               "    fcvt.l.d t1, f1\n";
    default: return {};
    }
}

uint64_t expectedT1(Producer p) { return p == Fp ? 7 : 8; }

/// Runs a program to the end and returns one integer register.
uint64_t runAndRead(ProcessorType type, const std::string &source, int reg)
{
    static QTemporaryDir dir;
    static int           serial = 0;
    const std::string    path = dir.path().toStdString() + "/nm" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << source;
    }
    setupVmStateDirectory();
    ProcessorManager manager;
    manager.changeProcessor(type);
    manager.setStepDelay(0);
    manager.loadProgram(assemble(path));
    manager.setBreakpoints({}); // known_issues 86
    manager.run();
    return manager.getRegisterFile()->ReadGpr(static_cast<size_t>(reg));
}

/// The smallest padding from which every larger padding (to kMaxPad) is
/// right too; kMaxPad + 1 if even kMaxPad is wrong.
int minimalPadding(ProcessorType type, Producer p)
{
    int minimal = kMaxPad + 1;
    for (int pad = kMaxPad; pad >= 0; --pad)
    {
        if (runAndRead(type, pairProgram(p, pad), 6) != expectedT1(p))
            break;
        minimal = pad;
    }
    return minimal;
}

struct Row
{
    int  pad[Producers];
    bool liWorks;
    bool laWorks;
};

const Row &measured(size_t model)
{
    static std::array<Row, 4> table{};
    static std::array<bool, 4> done{};
    if (!done[model])
    {
        const ProcessorType t = kModels[model].type;
        Row                &r = table[model];
        for (int p = 0; p < Producers; ++p)
            r.pad[p] = minimalPadding(t, static_cast<Producer>(p));
        r.liWorks = runAndRead(t, ".text\n    li t0, 0x12345678\n", 5) == 0x12345678u;
        r.laWorks = runAndRead(t, ".data\nV: .dword 7\n.text\n    la t0, V\n", 5) == 0x10000000u;
        done[model] = true;
    }
    return table[model];
}

class ProcessorNoteMeasured : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(ProcessorNoteMeasured, TheTable)
{
    const Model &m = kModels[GetParam()];
    const Row   &r = measured(GetParam());
    std::cout << "[measured] " << m.name << ": ALU " << r.pad[Alu] << ", load " << r.pad[Load]
              << ", FP " << r.pad[Fp] << " nops; li " << (r.liWorks ? "ok" : "WRONG") << ", la "
              << (r.laWorks ? "ok" : "WRONG") << "\n";
    for (int p = 0; p < Producers; ++p)
        EXPECT_LE(r.pad[p], kMaxPad) << kProducerName[p] << " is wrong even with " << kMaxPad
                                     << " nops";
}

TEST_P(ProcessorNoteMeasured, TheNoteStatesTheMeasuredTable)
{
    const Model      &m    = kModels[GetParam()];
    const Row        &r    = measured(GetParam());
    const std::string note = processorNote(true, m.hazard, m.forwarding);

    // The note's claims are fixed phrases, so each can be matched exactly:
    //   nothing needs padding     -> "Nothing to pad"
    //   only a load needs padding -> "a load needs N nop"
    //   every dependent pair      -> "needs N nops" (the worst case)
    //   li/la come out wrong      -> "give wrong results"
    const int worst = std::max({r.pad[Alu], r.pad[Load], r.pad[Fp]});
    std::string claim;
    if (worst == 0)
        claim = "Nothing to pad";
    else if (r.pad[Alu] == 0 && r.pad[Fp] == 0)
        claim = "a load needs " + std::to_string(r.pad[Load]) + " nop";
    else
        claim = "needs " + std::to_string(worst) + " nop";
    EXPECT_NE(note.find(claim), std::string::npos) << "note: " << note << "\nmissing: " << claim;

    const bool warns = note.find("give wrong results") != std::string::npos;
    EXPECT_EQ(warns, !(r.liWorks && r.laWorks))
        << "note: " << note << "\nli " << r.liWorks << ", la " << r.laWorks;
}

INSTANTIATE_TEST_SUITE_P(Pipelines, ProcessorNoteMeasured, ::testing::Values(0, 1, 2, 3),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kModels[info.param].name);
                         });

// The status bar's processor label carries the same note as its tooltip,
// so the chooser and the status bar say the same thing about the model in
// use (U5).
TEST(ProcessorNoteMeasured, TheStatusLabelCarriesTheSameNote)
{
    MainWindow window;
    QLabel    *label = window.findChild<QLabel *>("processorStatusLabel");
    ASSERT_NE(label, nullptr);
    const ProcessorType all[] = {ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF,
                                 ProcessorType::RV5Stage_H_NF, ProcessorType::RV5Stage_NH_F,
                                 ProcessorType::RV5Stage_H_F};
    for (ProcessorType t : all)
    {
        window.processorChanged(t);
        QApplication::processEvents();
        const ProcessorChoice c    = processorChoiceFor(t);
        const QString         note = QString::fromUtf8(processorNote(c.pipeline, c.hazard, c.forwarding));
        EXPECT_TRUE(label->toolTip().contains(note))
            << processorDescription(t) << "\ntooltip: " << label->toolTip().toStdString();
    }
}
