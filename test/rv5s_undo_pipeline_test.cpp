/**
 * @file rv5s_undo_pipeline_test.cpp
 * @brief Undo must restore the pipeline registers, including the field
 *        the forwarding fix added.
 *
 * `MEM_WB_Register` gained `prev_instruction` so that forwarding at the
 * MEM/WB stage can ask what the producer was, rather than guess from
 * `prev_reg_write`/`prev_freg_write`, which are both set for an F/D
 * instruction (known_issues 76, 79). Anything that snapshots pipeline
 * state has to carry it, or an Undo across a hazard window would leave
 * the next forwarding decision reading a stale instruction.
 *
 * It is carried, by construction rather than by a new line of code:
 * `RV5StageVM_Base::Step` snapshots whole structs
 * (`rv5s_processor_base.cpp:131-134`,
 * `old_mem_wb_reg = mem_wb_reg_;`), `PipelineRegisterChange` holds them
 * by value (`rv5s_processor_base.h:19`), and `Undo` assigns whole
 * structs back (`rv5s_processor_base.cpp:900-903`). So these tests were
 * green the day they were written. That is not proof they are watching
 * anything, which is why the stage report carries a control that drops
 * `prev_instruction` from the restore and shows this file going red.
 *
 * Method: run a program straight for T cycles; run it again stepping N,
 * undoing k, then stepping T - N + k more, so both execute the same
 * number of FORWARD cycles. Every architectural register, the cycle
 * count, the retired count and the PC must match.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <string>

using namespace Kites;
using namespace Kites::test_support;

namespace
{
struct Final
{
    std::array<uint64_t, 32> gpr{};
    std::array<uint64_t, 32> fpr{};
    unsigned int             cycles = 0;
    unsigned int             retired = 0;
    uint64_t                 pc = 0;
};

Final capture(ProcessorBase &vm)
{
    Final out;
    for (int i = 0; i < 32; ++i)
    {
        out.gpr[i] = vm.registers_.ReadGpr(i);
        out.fpr[i] = vm.registers_.ReadFpr(i);
    }
    out.cycles = vm.cycle_s_;
    out.retired = vm.instructions_retired_;
    out.pc = vm.program_counter_;
    return out;
}

/// Writes `body` to a file and loads it into a fresh processor.
std::unique_ptr<ProcessorBase> load(Model model, const QTemporaryDir &dir,
                                    const std::string &body, int serial)
{
    const std::string path =
        dir.path().toStdString() + "/undo" + std::to_string(serial) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    auto vm = makeProcessor(model);
    vm->LoadProgram(assemble(path));
    return vm;
}

void expectEqual(const Final &got, const Final &want, const std::string &what)
{
    SCOPED_TRACE(what);
    for (int i = 0; i < 32; ++i)
    {
        EXPECT_EQ(got.gpr[i], want.gpr[i]) << "x" << i << ": " << hex(got.gpr[i], 16)
                                           << " vs " << hex(want.gpr[i], 16);
        EXPECT_EQ(got.fpr[i], want.fpr[i]) << "f" << i << ": " << hex(got.fpr[i], 16)
                                           << " vs " << hex(want.fpr[i], 16);
    }
    EXPECT_EQ(got.cycles, want.cycles) << "cycle count";
    EXPECT_EQ(got.retired, want.retired) << "instructions retired";
    EXPECT_EQ(got.pc, want.pc) << "program counter";
}

/// The reported program: seven cycles straight, the dependent pair in
/// the middle of them.
const char *kReported = "    li t0, 3\n"
                        "    fcvt.d.l f1, t0\n"
                        "    fmadd.d f4, f1, f1, f1\n";

/// The integer-consumer case: an FP producer writing f10 and an `addi`
/// reading a0, which is x10. Twenty-six cycles straight.
std::string integerConsumer()
{
    return "    addi t3, x0, 3\n" + kGap + "    fcvt.d.l f1, t3\n" + kGap +
           "    fcvt.d.l f2, t3\n" + kGap + "    addi a0, x0, 90\n" + kGap +
           "    fadd.d f10, f1, f2\n" + "    addi s0, a0, 1\n";
}

int serial = 0;

/// Steps `n`, undoes `k`, then steps forward until the same number of
/// FORWARD cycles has been executed as a straight run of `total`.
void checkOne(Model model, const QTemporaryDir &dir, const std::string &body,
              const std::string &what, const Final &straight, int total, int n, int k)
{
    auto vm = load(model, dir, body, serial++);
    for (int i = 0; i < n; ++i)
        vm->Step();
    for (int i = 0; i < k; ++i)
        vm->Undo();
    for (int i = 0; i < total - n + k; ++i)
        vm->Step();
    expectEqual(capture(*vm), straight,
                what + " on " + name(model) + ": stepped " + std::to_string(n) + ", undid " +
                    std::to_string(k));
}

void checkProgram(Model model, const QTemporaryDir &dir, const std::string &body,
                  const std::string &what, int total)
{
    auto straightVm = load(model, dir, body, serial++);
    for (int i = 0; i < total; ++i)
        straightVm->Step();
    const Final straight = capture(*straightVm);

    // Family 1: undo the last k cycles and replay them. k = 1, 2, 3
    // reaches back across the writeback of the dependent instruction.
    for (int k = 1; k <= 3; ++k)
        checkOne(model, dir, body, what, straight, total, total, k);

    // Family 2: undo everything and replay the whole program, from every
    // depth. This replays the dependent chain itself, forwarding and
    // all, from a restored pipeline.
    for (int n = 2; n <= total; ++n)
        checkOne(model, dir, body, what, straight, total, n, n);

    // The third family - undo into the MIDDLE and continue - is not
    // asserted here, and not because it passes. Stepping forward after
    // an Undo that lands mid-program completes with every architectural
    // register unwritten: `li t0,3; fcvt.d.l f1,t0; fmadd.d f4,…` run
    // as step 3, undo 1, step 8 ends at the right PC (40) with the right
    // retired count (3) and x5 = f1 = f4 = 0. Measured identical at
    // 47ff007, before any of this branch's commits, so it is not a
    // regression from the forwarding work and it is not this stage's to
    // fix. docs/audit/known_issues.md 82.
}
} // namespace

TEST(Rv5sUndoPipeline, UndoAcrossTheHazardWindowOfTheReportedProgram)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // The straight run is 7 cycles, and the li -> fcvt.d.l -> fmadd.d
    // chain occupies the middle of them.
    for (Model model : {Model::Rv5HF, Model::Rv5NhF})
        checkProgram(model, dir, kReported, "the reported program", 7);
}

TEST(Rv5sUndoPipeline, UndoAcrossTheHazardWindowOfAnIntegerConsumer)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // The straight run is 26 cycles and the FP-producer/integer-consumer
    // pair is the last of them.
    for (Model model : {Model::Rv5HF, Model::Rv5NhF})
        checkProgram(model, dir, integerConsumer(), "the integer-consumer case", 26);
}
