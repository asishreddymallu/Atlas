/**
 * @file rv5s_undo_continue_test.cpp
 * @brief Undo, then keep stepping: the result must be the straight run.
 *
 * known_issues 82. An Undo that lands in the MIDDLE of a program and is
 * then continued finishes with the right PC, the right cycle count and
 * the right retired count, and the wrong architectural state - on the
 * reported program every register is left at 0, and on a program with a
 * store the store lands at the wrong address and overwrites the
 * program's own instruction bytes.
 *
 * `test/rv5s_undo_redo_test.cpp` never catches it because it always does
 * Step -> Undo -> Redo, which happens to leave the lost state holding
 * the value the next forward cycle wants. `test/rv5s_undo_pipeline_test.cpp`
 * misses it because it only undoes to cycle 0 or back from the very end.
 * The sequence the toolbar offers - Step, Undo, Step - is the one that
 * breaks, and it is what this file sweeps.
 *
 * Method: run a program straight for T cycles and keep that as the
 * oracle. Then, for every (N, k) with 1 <= k <= N <= T, run it again
 * stepping N, undoing k, and stepping on until the same number of
 * FORWARD cycles has been executed. Every GPR, every FPR, 512 bytes of
 * memory, the PC, the cycle count, the retired count, `stall_cycles_`
 * and `branch_mispredictions_` must match.
 *
 * Two sequences per cell:
 *   SUS  - step N, undo k, step (T - N + k)
 *   SURS - step N, undo k, redo k, step (T - N)
 * Undo -> Undo -> Step is SUS with k >= 2: `Undo()` is called k times in
 * a loop, which is that call sequence exactly, so it is swept rather
 * than duplicated.
 *
 * One assertion per (model, program), carrying the cell count, the
 * failing count, the first failing cell and the landing cycles, because
 * a per-cell EXPECT would print thousands of lines when it goes red.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace
{
/// Memory window compared on every cell. It covers the text segment of
/// every program here as well as the data address they store to, so a
/// store that goes astray is caught wherever it lands.
constexpr uint64_t kWatchBytes = 512;
/// Where the store/load program keeps its one value.
constexpr uint64_t kDataAddress = 256;

struct Final
{
    std::array<uint64_t, 32> gpr{};
    std::array<uint64_t, 32> fpr{};
    std::vector<uint8_t>     mem;
    uint64_t                 pc = 0;
    unsigned int             cycles = 0, retired = 0, stalls = 0, mispredictions = 0;
};

Final capture(ProcessorBase &vm)
{
    Final out;
    for (int i = 0; i < 32; ++i)
    {
        out.gpr[i] = vm.registers_.ReadGpr(i);
        out.fpr[i] = vm.registers_.ReadFpr(i);
    }
    out.mem.resize(kWatchBytes);
    for (uint64_t address = 0; address < kWatchBytes; ++address)
        out.mem[address] = vm.memory_controller_.peekByte(address);
    out.pc             = vm.program_counter_;
    out.cycles         = vm.cycle_s_;
    out.retired        = vm.instructions_retired_;
    out.stalls         = vm.stall_cycles_;
    out.mispredictions = vm.branch_mispredictions_;
    return out;
}

/// What differs, named. Empty means the two states are identical.
std::string difference(const Final &want, const Final &got)
{
    std::ostringstream out;
    for (int i = 0; i < 32; ++i)
        if (want.gpr[i] != got.gpr[i])
            out << " x" << i << "(" << hex(want.gpr[i], 16) << " vs " << hex(got.gpr[i], 16) << ")";
    for (int i = 0; i < 32; ++i)
        if (want.fpr[i] != got.fpr[i])
            out << " f" << i << "(" << hex(want.fpr[i], 16) << " vs " << hex(got.fpr[i], 16) << ")";
    for (uint64_t address = 0; address < kWatchBytes; ++address)
        if (want.mem[address] != got.mem[address])
            out << " mem[" << address << "]";
    if (want.pc != got.pc)
        out << " pc(" << want.pc << " vs " << got.pc << ")";
    if (want.cycles != got.cycles)
        out << " cycles(" << want.cycles << " vs " << got.cycles << ")";
    if (want.retired != got.retired)
        out << " retired(" << want.retired << " vs " << got.retired << ")";
    if (want.stalls != got.stalls)
        out << " stall_cycles(" << want.stalls << " vs " << got.stalls << ")";
    if (want.mispredictions != got.mispredictions)
        out << " mispredictions(" << want.mispredictions << " vs " << got.mispredictions << ")";
    return out.str();
}

int serial = 0;

std::unique_ptr<ProcessorBase> load(Model model, const QTemporaryDir &dir,
                                    const std::string &body)
{
    const std::string path =
        dir.path().toStdString() + "/uc" + std::to_string(serial++) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    auto vm = makeProcessor(model);
    vm->LoadProgram(assemble(path));
    return vm;
}

struct Sweep
{
    int           cells = 0;
    int           failing = 0;
    std::string   first;
    std::set<int> landings; ///< the N - k that failed, i.e. the cycle the Undo landed on
};

/// Sweeps every (N, k) in both sequences and reports what diverged.
Sweep sweep(Model model, const QTemporaryDir &dir, const std::string &body, int total,
            const Final &straight)
{
    Sweep report;
    for (const bool redo : {false, true})
    {
        for (int n = 1; n <= total; ++n)
        {
            for (int k = 1; k <= n; ++k)
            {
                auto vm = load(model, dir, body);
                for (int i = 0; i < n; ++i)
                    vm->Step();
                for (int i = 0; i < k; ++i)
                    vm->Undo();
                int forward = total - n + k;
                if (redo)
                {
                    for (int i = 0; i < k; ++i)
                        vm->Redo();
                    forward = total - n;
                }
                for (int i = 0; i < forward; ++i)
                    vm->Step();

                ++report.cells;
                const std::string what = difference(straight, capture(*vm));
                if (!what.empty())
                {
                    ++report.failing;
                    report.landings.insert(n - k);
                    if (report.first.empty())
                        report.first = std::string(redo ? "step/undo/redo/step" : "step/undo/step") +
                                       " N=" + std::to_string(n) + " k=" + std::to_string(k) +
                                       " ->" + what;
                }
            }
        }
    }
    return report;
}

/// Runs the straight oracle, sweeps against it, and asserts once.
Final check(Model model, const QTemporaryDir &dir, const std::string &body,
            const std::string &what, int total)
{
    auto straightVm = load(model, dir, body);
    for (int i = 0; i < total; ++i)
        straightVm->Step();
    const Final straight = capture(*straightVm);

    const Sweep report = sweep(model, dir, body, total, straight);

    std::ostringstream landings;
    for (int landing : report.landings)
        landings << " " << landing;
    EXPECT_EQ(report.failing, 0)
        << what << " on " << name(model) << ": " << report.failing << " of " << report.cells
        << " (N, k) cells end somewhere other than the straight run of " << total
        << " cycles.\n  landing cycles (N - k) that diverge:" << landings.str()
        << "\n  first: " << report.first;
    return straight;
}

/// The reported program, with four nops between each dependent pair so
/// that every model, hazard detection or not, computes the same thing.
std::string reportedPadded()
{
    return "    li t0, 3\n" + kGap + "    fcvt.d.l f1, t0\n" + kGap +
           "    fmadd.d f4, f1, f1, f1\n";
}

/// The same program unpadded. What it computes is model-dependent by
/// design (known_issues 77), so only the replay equality is asserted.
const char *kReportedUnpadded = "    li t0, 3\n"
                                "    fcvt.d.l f1, t0\n"
                                "    fmadd.d f4, f1, f1, f1\n";

/// A store and a load back, padded. Written with x0 as the base so the
/// address needs no `li` chain: the ALU row for `sd`/`ld` is the one an
/// Undo loses, and a wrong row sends the store somewhere else entirely.
std::string storeAndLoad()
{
    return "    addi t2, x0, 90\n" + kGap + "    sd t2, 256(x0)\n" + kGap +
           "    ld t3, 256(x0)\n" + kGap + "    addi t4, t3, 1\n";
}

/// A loop with a taken backward branch, padded, so the sweep covers a
/// pipeline flush and `branch_mispredictions_`.
std::string takenBranchLoop()
{
    return "    addi t0, x0, 3\n"
           "loop:\n"
           "    addi t0, t0, -1\n" +
           kGap + "    bne t0, x0, loop\n" + kGap + "    addi t1, x0, 7\n";
}

/// Cycle budgets: enough for the padded programs to drain on the slowest
/// model, and for the loop to go round more than once. The loop does not
/// terminate on nh-nf, which has neither hazard detection nor
/// forwarding, so every program here is driven for a FIXED number of
/// cycles rather than to completion.
constexpr int kReportedPaddedCycles = 16;
constexpr int kReportedUnpaddedCycles = 12;
constexpr int kStoreAndLoadCycles = 21;
constexpr int kBranchLoopCycles = 20;

/// Every model and every program, for one model.
void checkEverything(Model model)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const Final padded =
        check(model, dir, reportedPadded(), "the reported program, padded", kReportedPaddedCycles);
    // The oracle is only worth anything if the straight run computed the
    // right answer, so pin it. Padded, all five models must agree.
    EXPECT_EQ(padded.gpr[5], 3u) << "x5 after the straight run on " << name(model);
    EXPECT_EQ(padded.fpr[1], 0x4008000000000000ULL) << "f1 on " << name(model);
    EXPECT_EQ(padded.fpr[4], 0x4028000000000000ULL) << "f4 on " << name(model);

    check(model, dir, kReportedUnpadded, "the reported program, unpadded",
          kReportedUnpaddedCycles);

    const Final stored =
        check(model, dir, storeAndLoad(), "a store and a load back", kStoreAndLoadCycles);
    EXPECT_EQ(stored.gpr[7], 90u) << "t2 on " << name(model);
    EXPECT_EQ(stored.gpr[28], 90u) << "t3, loaded back, on " << name(model);
    EXPECT_EQ(stored.gpr[29], 91u) << "t4 on " << name(model);
    EXPECT_EQ(stored.mem[kDataAddress], 90u) << "the stored byte on " << name(model);

    // The loop's architectural result after a fixed 20 cycles depends on
    // the model, so only the replay equality is asserted for it.
    check(model, dir, takenBranchLoop(), "a loop with a taken branch", kBranchLoopCycles);
}
} // namespace

TEST(Rv5sUndoContinue, Rvss)
{
    checkEverything(Model::Rvss);
}

TEST(Rv5sUndoContinue, Rv5NoHazardNoForwarding)
{
    checkEverything(Model::Rv5NhNf);
}

TEST(Rv5sUndoContinue, Rv5NoHazardForwarding)
{
    checkEverything(Model::Rv5NhF);
}

TEST(Rv5sUndoContinue, Rv5HazardNoForwarding)
{
    checkEverything(Model::Rv5HNf);
}

TEST(Rv5sUndoContinue, Rv5HazardForwarding)
{
    checkEverything(Model::Rv5HF);
}
