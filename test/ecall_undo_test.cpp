/**
 * @file ecall_undo_test.cpp
 * @brief Undo across an ecall, on all five models.
 *
 * E1 (known_issues 66) made the pipelines serve an ecall at writeback, then
 * flush the younger stages and redirect the PC (serve_syscall_at_writeback).
 * That is state the step delta has to carry for Undo to be exact. Three
 * programs - print an int, read (input pushed before the run), exit - each
 * checked two ways:
 *
 *   1. Step to the end, Undo every step: the whole state equals the state
 *      LoadProgram left - all 32 x and f registers, the data memory, the
 *      PC, the cycle / retired / stall / misprediction counters.
 *   2. Step k, Undo j, Step j again: the state equals an uninterrupted run
 *      of k steps, for every k and for j in {1, 2, 3, k}.
 *
 * Outputs are an EVENT LOG, not restorable state (the user's decision):
 * a printed line is not retracted by Undo, so after step k / undo j /
 * step j the output must be the uninterrupted k steps' output followed by
 * exactly the re-executed j steps' lines again.
 *
 * Every program is padded (four nops after each instruction), so the
 * no-hazard models are inside their contract.
 *
 * Not covered: re-executing a READ after undoing it. The consumed input
 * line is not part of the step delta, so the re-executed read waits
 * forever, on every model (known_issues 112). Those (k, j) sequences are
 * skipped and counted; the rest of the Read program is checked.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace Kites::test_support
{
void PrintTo(Model model, std::ostream *os);
} // namespace Kites::test_support

namespace
{
constexpr uint64_t kDataBase = 0x10000000ULL;
constexpr int kDataBytes = 64;

struct Snapshot
{
    std::array<uint64_t, 32> x{};
    std::array<uint64_t, 32> f{};
    std::array<uint8_t, kDataBytes> mem{};
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
    for (int i = 0; i < kDataBytes; ++i)
        s.mem[i] = p.memory_controller_.peekByte(kDataBase + i);
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
    for (int i = 0; i < kDataBytes; ++i)
        if (a.mem[i] != b.mem[i])
            out += " mem+" + std::to_string(i);
    if (a.pc != b.pc)
        out += " pc:" + std::to_string(a.pc) + "!=" + std::to_string(b.pc);
    if (a.cycles != b.cycles)
        out += " cycles:" + std::to_string(a.cycles) + "!=" + std::to_string(b.cycles);
    if (a.retired != b.retired)
        out += " retired:" + std::to_string(a.retired) + "!=" + std::to_string(b.retired);
    if (a.stalls != b.stalls)
        out += " stalls";
    if (a.mispredictions != b.mispredictions)
        out += " mispredictions";
    return out;
}

std::string padded(const std::vector<std::string> &lines)
{
    std::string out = ".text\n";
    for (const std::string &line : lines)
        out += "    " + line + "\n" + kGap;
    return out;
}

struct UndoProgram
{
    const char *name;
    std::vector<std::string> lines;
    std::vector<std::string> input;
};

const UndoProgram kPrograms[] = {
    {"PrintInt", {"li a0, -1234", "li a7, 1", "ecall", "li s2, 5"}, {}},
    {"Read",
     {"li a0, 0", "li a1, 0x10000000", "li a2, 16", "li a7, 63", "ecall", "mv s0, a0",
      "lbu t0, 0(a1)"},
     {"42"}},
    {"Exit", {"li a0, 0", "li a7, 10", "li s2, 5", "ecall", "li s3, 9"}, {}},
};

/// A processor with the program loaded and its input queued, and every
/// call into it made on a watchdog thread.
struct Machine
{
    std::unique_ptr<ProcessorBase> p;
    bool hung = false;

    Machine(Model model, const QTemporaryDir &dir, const UndoProgram &program)
    {
        const std::string path =
            dir.path().toStdString() + "/eu" + std::to_string(fileSerial++) + ".s";
        {
            std::ofstream file(path);
            file << padded(program.lines);
        }
        setupVmStateDirectory();
        p = makeProcessor(model);
        p->LoadProgram(assemble(path));
        p->SetBreakpoints({});
        for (const std::string &line : program.input)
            p->PushInput(line);
    }
    ~Machine()
    {
        if (hung)
            (void)p.release(); // still in use by the abandoned thread
    }

    /// Runs `body` with stdout captured; returns what it printed.
    std::string guarded(const std::function<void(ProcessorBase &)> &body)
    {
        if (hung)
            return {};
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> finished = done->get_future();
        ProcessorBase *raw = p.get();
        testing::internal::CaptureStdout();
        std::thread worker(
            [raw, body, done]()
            {
                body(*raw);
                done->set_value();
            });
        hung = finished.wait_for(std::chrono::seconds(3)) != std::future_status::ready;
        const std::string out = testing::internal::GetCapturedStdout();
        if (hung)
            worker.detach();
        else
            worker.join();
        return out;
    }
    std::string step(int n)
    {
        return guarded([n](ProcessorBase &q)
                       {
                           for (int i = 0; i < n; ++i)
                               q.StepAndNotify();
                       });
    }
    void undo(int n)
    {
        guarded([n](ProcessorBase &q)
                {
                    for (int i = 0; i < n; ++i)
                        q.Undo();
                });
    }
};

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

/// The uninterrupted run: the number of steps until StepAndNotify reports
/// the end, the state after every step, and what each step printed.
struct StraightRun
{
    int steps = 0;
    std::vector<Snapshot> after; ///< after[k] = state after k steps
    std::vector<std::string> printed; ///< printed[k] = output of step k (1-based)
    bool hung = false;
};

StraightRun straight(Model model, const QTemporaryDir &dir, const UndoProgram &program)
{
    StraightRun run;
    Machine m(model, dir, program);
    run.after.push_back(snap(*m.p));
    run.printed.push_back({});
    for (int k = 1; k <= 400; ++k)
    {
        bool more = false;
        run.printed.push_back(m.guarded([&more](ProcessorBase &q) { more = q.StepAndNotify(); }));
        if (m.hung)
        {
            run.hung = true;
            return run;
        }
        run.after.push_back(snap(*m.p));
        run.steps = k;
        if (!more)
            break;
    }
    return run;
}
} // namespace

class EcallUndo : public testing::TestWithParam<Param>
{
};

INSTANTIATE_TEST_SUITE_P(AllModels, EcallUndo, testing::ValuesIn(allParams()),
                         [](const testing::TestParamInfo<Param> &info)
                         {
                             std::string label = std::string(kPrograms[info.param.program].name) +
                                                 "_" + name(info.param.model);
                             for (char &c : label)
                                 if (c == '-')
                                     c = '_';
                             return label;
                         });

TEST_P(EcallUndo, StepToTheEndThenUndoToTheStart)
{
    const UndoProgram &program = kPrograms[GetParam().program];
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const StraightRun run = straight(GetParam().model, dir, program);
    ASSERT_FALSE(run.hung) << "the uninterrupted run hung";
    ASSERT_GT(run.steps, 0);

    Machine m(GetParam().model, dir, program);
    m.step(run.steps);
    ASSERT_FALSE(m.hung);
    EXPECT_EQ(snap(*m.p), run.after.back()) << describe(snap(*m.p), run.after.back());
    m.undo(run.steps);
    ASSERT_FALSE(m.hung);
    const Snapshot back = snap(*m.p);
    EXPECT_EQ(back, run.after.front()) << "after undoing all " << run.steps << " steps:"
                                       << describe(back, run.after.front());
}

TEST_P(EcallUndo, StepKUndoJStepJMatchesAnUninterruptedRun)
{
    const UndoProgram &program = kPrograms[GetParam().program];
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const StraightRun run = straight(GetParam().model, dir, program);
    ASSERT_FALSE(run.hung) << "the uninterrupted run hung";

    // The step that served the read, if any. Undoing it does not give the
    // consumed input line back to the queue (known_issues 112): stepping it
    // again waits for input that never comes, on every model. Those
    // sequences are skipped - counted, not asserted - until 112 is fixed.
    int readStep = 0;
    for (int s = 1; s <= run.steps; ++s)
        if (run.printed[s].find("VM_STDIN_START") != std::string::npos)
            readStep = s;
    if (!program.input.empty())
        ASSERT_GT(readStep, 0) << "the read never ran";

    int failures = 0, checked = 0, skipped = 0;
    for (int k = 1; k <= run.steps && failures < 5; ++k)
    {
        for (int j : {1, 2, 3, k})
        {
            if (j > k || (j == k && k <= 3))
                continue;
            if (readStep > 0 && k - j + 1 <= readStep && readStep <= k)
            {
                ++skipped;
                continue;
            }
            ++checked;
            Machine m(GetParam().model, dir, program);
            std::string out = m.step(k);
            m.undo(j);
            out += m.step(j);
            if (m.hung)
            {
                ADD_FAILURE() << "step " << k << ", undo " << j << ", step " << j << ": hung";
                ++failures;
                break;
            }
            std::string expected;
            for (int s = 1; s <= k; ++s)
                expected += run.printed[s];
            for (int s = k - j + 1; s <= k; ++s)
                expected += run.printed[s];
            const Snapshot got = snap(*m.p);
            if (!(got == run.after[k]) || out != expected)
            {
                ADD_FAILURE() << "step " << k << ", undo " << j << ", step " << j << ":"
                              << describe(got, run.after[k])
                              << (out != expected ? " output:[" + out + "] expected:[" +
                                                        expected + "]"
                                                  : "");
                ++failures;
            }
        }
    }
    EXPECT_GT(checked, 0);
    RecordProperty("sequences_checked", checked);
    RecordProperty("sequences_skipped_for_112", skipped);
}
