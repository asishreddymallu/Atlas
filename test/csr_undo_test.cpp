/**
 * @file csr_undo_test.cpp
 * @brief Undo across CSR instructions and FP ops that write the flags, on all
 *        five models (known_issues 104 and 115).
 *
 * The pipelines now serve a CSR instruction at writeback and flush the
 * younger stages (serve_csr_at_writeback), and every FP op writes its status
 * into fflags when it retires. Both are state the step delta has to carry:
 *
 *   1. Step to the end, Undo every step: the whole state - all x and f
 *      registers, fflags/frm/fcsr, the PC and the counters - equals what
 *      LoadProgram left.
 *   2. Step k, Undo j, Step j again equals an uninterrupted run of k steps,
 *      for every k and j in {1, 2, 3, k}.
 *
 * Padded programs (four nops after each instruction).
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace Kites::test_support
{
void PrintTo(Model model, std::ostream *os);
} // namespace Kites::test_support

namespace
{
struct Snapshot
{
    std::array<uint64_t, 32> x{};
    std::array<uint64_t, 32> f{};
    std::array<uint64_t, 3> csr{}; ///< fflags, frm, fcsr
    uint64_t pc = 0;
    unsigned cycles = 0, retired = 0, stalls = 0, mispredictions = 0;
    bool operator==(const Snapshot &) const = default;
};

Snapshot snap(ProcessorBase &p)
{
    Snapshot s;
    for (int i = 0; i < 32; ++i)
    {
        s.x[i] = p.registers_.ReadGpr(i);
        s.f[i] = p.registers_.ReadFpr(i);
    }
    for (int i = 0; i < 3; ++i)
        s.csr[i] = p.registers_.ReadCsr(i + 1);
    s.pc = p.program_counter_;
    s.cycles = p.cycle_s_;
    s.retired = p.instructions_retired_;
    s.stalls = p.stall_cycles_;
    s.mispredictions = p.branch_mispredictions_;
    return s;
}

std::string describe(const Snapshot &a, const Snapshot &b)
{
    std::string out;
    for (int i = 0; i < 32; ++i)
        if (a.x[i] != b.x[i])
            out += " x" + std::to_string(i) + ":" + hex(a.x[i], 1) + "!=" + hex(b.x[i], 1);
    for (int i = 0; i < 32; ++i)
        if (a.f[i] != b.f[i])
            out += " f" + std::to_string(i);
    static const char *const kNames[] = {"fflags", "frm", "fcsr"};
    for (int i = 0; i < 3; ++i)
        if (a.csr[i] != b.csr[i])
            out += std::string(" ") + kNames[i] + ":" + hex(a.csr[i], 1) + "!=" + hex(b.csr[i], 1);
    if (a.pc != b.pc)
        out += " pc:" + std::to_string(a.pc) + "!=" + std::to_string(b.pc);
    if (a.cycles != b.cycles || a.retired != b.retired || a.stalls != b.stalls ||
        a.mispredictions != b.mispredictions)
        out += " counters";
    return out;
}

struct UndoProgram
{
    const char *name;
    std::vector<std::string> lines;
};

const UndoProgram kPrograms[] = {
    {"CsrForms",
     {"li x6, 0xA5", "csrrw x5, fcsr, x6", "csrrs x7, fflags, x0", "csrrwi x8, frm, 3",
      "csrrc x9, fcsr, x6", "csrrsi x10, fflags, 17", "csrrci x11, fcsr, 1", "li x12, 7"}},
    {"FpFlagsAndFrm",
     {"li x6, 1", "fcvt.s.w f1, x6", "fcvt.s.w f2, x0", "fdiv.s f3, f1, f2", "csrrs x5, fflags, x0",
      "csrrwi x7, frm, 3", "li x8, 3", "fcvt.s.w f4, x8", "fdiv.s f5, f1, f4",
      "csrrw x9, fcsr, x0", "fdiv.s f6, f1, f4", "li x10, 9"}},
    // E5 (72): flags accrue - DZ, NX, then a double DZ and NV on top, an fsw
    // that must not clear them, one read, then an exact op. Each Undo has to
    // put back the OR it removed, not zero.
    {"FpFlagAccrual",
     {"li x6, 1", "li x8, 3", "li x9, -1", "lui x11, 0x10000", "fcvt.s.w f1, x6",
      "fcvt.s.w f2, x0", "fcvt.s.w f4, x8", "fdiv.s f3, f1, f2", "fdiv.s f5, f1, f4",
      "fcvt.d.w f6, x6", "fcvt.d.w f7, x0", "fdiv.d f8, f6, f7", "fcvt.d.w f10, x9",
      "fsqrt.d f11, f10", "fsw f3, 0(x11)", "csrrs x5, fflags, x0", "fadd.s f12, f1, f4",
      "li x12, 7"}},
};

std::string padded(const std::vector<std::string> &lines)
{
    std::string out = ".text\n";
    for (const std::string &line : lines)
        out += "    " + line + "\n" + kGap;
    return out;
}

std::unique_ptr<ProcessorBase> machine(Model model, const QTemporaryDir &dir,
                                       const UndoProgram &program)
{
    const std::string path = dir.path().toStdString() + "/cu" + std::to_string(fileSerial++) + ".s";
    {
        std::ofstream file(path);
        file << padded(program.lines);
    }
    setupVmStateDirectory();
    auto p = makeProcessor(model);
    p->LoadProgram(assemble(path));
    p->SetBreakpoints({});
    return p;
}

struct Param
{
    std::size_t program;
    Model       model;
};

void PrintTo(const Param &param, std::ostream *os)
{
    *os << kPrograms[param.program].name << "/" << name(param.model);
}

std::vector<Param> allParams()
{
    std::vector<Param> out;
    for (std::size_t i = 0; i < std::size(kPrograms); ++i)
        for (Model model : allModels())
            out.push_back({i, model});
    return out;
}

/// after[k] = the state after k uninterrupted steps; after.size()-1 steps end the run.
std::vector<Snapshot> straight(Model model, const QTemporaryDir &dir, const UndoProgram &program)
{
    auto p = machine(model, dir, program);
    std::vector<Snapshot> after{snap(*p)};
    for (int k = 1; k <= 600; ++k)
    {
        const bool more = p->StepAndNotify();
        after.push_back(snap(*p));
        if (!more)
            break;
    }
    return after;
}
} // namespace

class CsrUndo : public testing::TestWithParam<Param>
{
};

INSTANTIATE_TEST_SUITE_P(AllModels, CsrUndo, testing::ValuesIn(allParams()),
                         [](const testing::TestParamInfo<Param> &info)
                         {
                             std::string label = std::string(kPrograms[info.param.program].name) +
                                                 "_" + name(info.param.model);
                             for (char &c : label)
                                 if (c == '-')
                                     c = '_';
                             return label;
                         });

TEST_P(CsrUndo, StepToTheEndThenUndoToTheStart)
{
    const UndoProgram &program = kPrograms[GetParam().program];
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<Snapshot> run = straight(GetParam().model, dir, program);
    const int steps = static_cast<int>(run.size()) - 1;
    ASSERT_GT(steps, 0);

    auto p = machine(GetParam().model, dir, program);
    for (int i = 0; i < steps; ++i)
        p->StepAndNotify();
    EXPECT_EQ(snap(*p), run.back()) << describe(snap(*p), run.back());
    for (int i = 0; i < steps; ++i)
        p->Undo();
    EXPECT_EQ(snap(*p), run.front()) << "after undoing all " << steps << " steps:"
                                     << describe(snap(*p), run.front());
}

TEST_P(CsrUndo, StepKUndoJStepJMatchesAnUninterruptedRun)
{
    const UndoProgram &program = kPrograms[GetParam().program];
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<Snapshot> run = straight(GetParam().model, dir, program);
    const int steps = static_cast<int>(run.size()) - 1;

    int failures = 0, checked = 0;
    for (int k = 1; k <= steps && failures < 5; ++k)
    {
        for (int j : {1, 2, 3, k})
        {
            if (j > k || (j == k && k <= 3))
                continue;
            ++checked;
            auto p = machine(GetParam().model, dir, program);
            for (int i = 0; i < k; ++i)
                p->StepAndNotify();
            for (int i = 0; i < j; ++i)
                p->Undo();
            for (int i = 0; i < j; ++i)
                p->StepAndNotify();
            const Snapshot got = snap(*p);
            if (!(got == run[k]))
            {
                ADD_FAILURE() << "step " << k << ", undo " << j << ", step " << j << ":"
                              << describe(got, run[k]);
                ++failures;
            }
        }
    }
    EXPECT_GT(checked, 0);
    RecordProperty("sequences_checked", checked);
}
