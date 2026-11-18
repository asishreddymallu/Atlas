/**
 * @file ecall_syscall_test.cpp
 * @brief `ecall` serves the same system calls on all five models as on rvss.
 *
 * rvss's service table (ProcessorBase::ServeSyscall, formerly
 * RVSSProcessor::HandleSyscall): a7 = 1 print int, 2 print float, 3 print
 * double, 4 print string, 10 exit, 63 read (fd 0, with an input wait), 64
 * write (fd 1); anything else prints "Unknown syscall number" on stderr and
 * carries on. Before known_issues 66 was fixed, only rv5-nh-nf reacted to
 * an ecall at all, and it treated every one as exit; the other three
 * pipelines ran it as an addi-shaped no-op.
 *
 * The pipelines serve an ecall when it reaches WRITEBACK: every older
 * instruction has retired, so a0/a1/a2/a7 are architectural, an exit
 * drains what came before it by construction, and the younger
 * instructions in flight are flushed and refetched from ecall + 4.
 *
 * ── what each model owes ───────────────────────────────────────────────
 *
 *   rvss, h_nf, h_f   the exact result, padded and unpadded.
 *   nh_nf, nh_f       ONLY THE PADDED FORM is asserted. Neither has hazard
 *                     detection, so an unpadded program is wrong on them
 *                     by design - here the echo program's `lbu` feeds an
 *                     `addi` back to back, a load-use pair even nh_f
 *                     cannot forward. Each unpadded case still runs on
 *                     them (it must not hang or crash), but its result is
 *                     not checked.
 *
 * "Padded" puts four nops after every instruction (float_test_support.h's
 * kGap). Every `li` here is a single instruction (a 12-bit value, or a
 * bare lui for the data base), so the padding covers every pair.
 *
 * Expected output and register values were computed in Python, not read
 * back from rvss.
 *
 * Every run happens on a watchdog thread: a regression here is far more
 * likely to hang (an input wait, a stop that deadlocks) than to fail an
 * assertion, and a hang must fail one test, not stall the suite.
 */
#include "float_test_support.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <future>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace Kites;
using namespace Kites::test_support;

namespace Kites::test_support
{
/// The model's name in test output and in the ctest names gtest_discover_tests
/// builds, instead of "4-byte object <...>".
void PrintTo(Model model, std::ostream *os)
{
    *os << name(model);
}
} // namespace Kites::test_support

namespace
{
/// The data section's default base (config.h). Loaded with `li` (a single
/// lui), not `la`: `la` expands to auipc, which the rv5s variants compute
/// wrong (a separate known_issues entry).
constexpr uint64_t kDataBase = 0x10000000ULL;

constexpr int kWatchdogMs = 10000;

/// Set while a test program is running. rvss's exit service used to call
/// exit(0) (known_issues 101): that ends kites_test with status 0 halfway
/// through a test, and ctest then reports the test as PASSED. This turns
/// any exit() during a run into exit status 1, which ctest does see (the
/// message itself may land in a stdout capture and never be shown).
std::atomic<bool> gProgramRunning{false};

void failIfExitedDuringARun()
{
    if (gProgramRunning)
    {
        std::fputs("\nFAILED: exit() was called while a simulated program was running\n",
                   stdout);
        std::fflush(stdout);
        std::_Exit(1);
    }
}

[[maybe_unused]] const bool kExitGuardInstalled = (std::atexit(failIfExitedDuringARun), true);

/// Marks a run for failIfExitedDuringARun.
struct RunningProgram
{
    RunningProgram() { gProgramRunning = true; }
    ~RunningProgram() { gProgramRunning = false; }
};

bool owesUnpadded(Model model)
{
    return model == Model::Rvss || model == Model::Rv5HNf || model == Model::Rv5HF;
}

/// The lines a system call prints, in order. rvss's Step() also traces
/// every instruction to stdout ("Program Counter: ...", "VM_STEP_..."),
/// which is not what these tests are about.
std::string syscallLines(const std::string &captured)
{
    static const char *const kPrefixes[] = {"[Syscall output:", "VM_STDIN_", "VM_STDOUT_",
                                            "VM_EXIT", "Exited with exit code:"};
    std::string kept;
    std::istringstream in(captured);
    std::string line;
    while (std::getline(in, line))
    {
        for (const char *prefix : kPrefixes)
        {
            if (line.rfind(prefix, 0) == 0)
            {
                kept += line + "\n";
                break;
            }
        }
    }
    return kept;
}

/// One instruction per entry; padded puts kGap after each.
std::string program(const std::vector<std::string> &lines, bool padded)
{
    std::string out;
    for (const std::string &line : lines)
    {
        out += "    " + line + "\n";
        if (padded)
            out += kGap;
    }
    return out;
}

/// Runs `body` on its own thread. False if it did not finish in time; the
/// thread is then abandoned, and the caller must leak whatever it touches.
bool finishesWithin(int ms, std::function<void()> body)
{
    auto done = std::make_shared<std::promise<void>>();
    std::future<void> finished = done->get_future();
    std::thread worker(
        [body = std::move(body), done]()
        {
            body();
            done->set_value();
        });
    if (finished.wait_for(std::chrono::milliseconds(ms)) == std::future_status::ready)
    {
        worker.join();
        return true;
    }
    worker.detach();
    gProgramRunning = false; // a hang, reported as one - not as an exit()
    return false;
}

struct Result
{
    std::unique_ptr<ProcessorBase> processor;
    bool                           finished = false;
    std::string                    out;
    std::string                    err;
    uint64_t gpr(int index) const { return processor->registers_.ReadGpr(index); }
    uint64_t dword(uint64_t address) const
    {
        return processor->memory_controller_.readDoubleWord_d(address);
    }
};

std::unique_ptr<ProcessorBase> load(Model model, const QTemporaryDir &dir, const std::string &body)
{
    const std::string path =
        dir.path().toStdString() + "/ec" + std::to_string(fileSerial++) + ".s";
    {
        std::ofstream file(path);
        file << ".text\n" << body;
    }
    setupVmStateDirectory();
    auto processor = makeProcessor(model);
    processor->LoadProgram(assemble(path));
    processor->SetBreakpoints({}); // LoadProgram plants one at the end (known_issues 86)
    processor->step_delay_ = 0;
    return processor;
}

/// Run() - the worker's entry point - with stdout and stderr captured.
Result runProgram(Model model, const QTemporaryDir &dir, const std::string &body,
                  const std::vector<std::string> &input = {})
{
    Result r;
    r.processor = load(model, dir, body);
    for (const std::string &line : input)
        r.processor->PushInput(line);
    ProcessorBase *p = r.processor.get();
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    r.finished = finishesWithin(kWatchdogMs,
                                [p]()
                                {
                                    RunningProgram running;
                                    p->Run();
                                });
    r.out = syscallLines(testing::internal::GetCapturedStdout());
    r.err = testing::internal::GetCapturedStderr();
    if (!r.finished)
        (void)r.processor.release(); // still in use by the abandoned thread
    return r;
}

/// Runs a case unpadded and padded on one model; `check` applies the
/// model's contract (owesUnpadded) and holds the assertions.
void forEachPadding(Model model, const std::function<void(Model, bool padded)> &check)
{
    for (bool padded : {false, true})
    {
        SCOPED_TRACE(padded ? "padded" : "unpadded");
        check(model, padded);
    }
}

const std::string kExitLines = "VM_EXIT\nExited with exit code: 0\n";
} // namespace

/// One test per (case, model), so each model is its own ctest process and
/// its own failing NAME: a model that kills or hangs the process cannot
/// hide what the others do.
class EcallSyscall : public testing::TestWithParam<Model>
{
};

INSTANTIATE_TEST_SUITE_P(AllModels, EcallSyscall, testing::ValuesIn(allModels()),
                         [](const testing::TestParamInfo<Model> &info)
                         {
                             std::string label = name(info.param);
                             for (char &c : label)
                                 if (c == '-')
                                     c = '_';
                             return label;
                         });

// ───────── a7 = 1: print an integer ─────────

TEST_P(EcallSyscall, PrintsAnInteger)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<std::string> lines = {"li a0, -1234", "li a7, 1", "ecall",
                                            "li a0, 0",     "li a7, 10", "ecall"};
    forEachPadding(GetParam(),
        [&](Model model, bool padded)
        {
            const Result r = runProgram(model, dir, program(lines, padded));
            ASSERT_TRUE(r.finished) << "the run did not finish";
            if (!padded && !owesUnpadded(model))
                return; // no hazard detection: only the padded form is owed
            EXPECT_EQ(r.out, "[Syscall output: -1234]\n" + kExitLines);
            EXPECT_EQ(r.gpr(10), 0u);
            EXPECT_EQ(r.gpr(17), 10u);
        });
}

// ───────── a7 = 63 then 1: read an integer and echo it ─────────

namespace
{
/// read(0, kDataBase, 16), keep the count in s0, parse two ASCII digits,
/// print the number, exit.
const std::vector<std::string> kEchoLines = {
    "li a0, 0",       "li a1, 0x10000000", "li a2, 16",       "li a7, 63",
    "ecall",          "mv s0, a0",         "lbu t0, 0(a1)",   "lbu t1, 1(a1)",
    "addi t0, t0, -48", "addi t1, t1, -48", "li t2, 10",      "mul t0, t0, t2",
    "add a0, t0, t1", "li a7, 1",          "ecall",           "li a0, 0",
    "li a7, 10",      "ecall"};

/// "42": the read prints its two markers, then the echo, then the exit.
const std::string kEchoOut =
    "VM_STDIN_START\nVM_STDIN_END\n[Syscall output: 42]\n" + kExitLines;

/// Python: struct.unpack('<Q', b'42\x00' + bytes(5))[0]
constexpr uint64_t kEchoBuffer = 0x3234;

void checkEcho(const Result &r)
{
    EXPECT_EQ(r.out, kEchoOut);
    EXPECT_EQ(r.gpr(8), 2u) << "a0 after the read is the byte count";
    EXPECT_EQ(r.dword(kDataBase), kEchoBuffer);
}
} // namespace

TEST_P(EcallSyscall, ReadsAnIntegerAndEchoesIt)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    forEachPadding(GetParam(),
        [&](Model model, bool padded)
        {
            const Result r = runProgram(model, dir, program(kEchoLines, padded), {"42"});
            ASSERT_TRUE(r.finished) << "the run did not finish";
            if (!padded && !owesUnpadded(model))
                return;
            checkEcho(r);
        });
}

TEST_P(EcallSyscall, ReadWaitsForInputThatArrivesLater)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const Model model = GetParam();
    {
        Result r;
        r.processor = load(model, dir, program(kEchoLines, true));
        ProcessorBase *p = r.processor.get();
        testing::internal::CaptureStdout();
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> finished = done->get_future();
        std::thread worker(
            [p, done]()
            {
                {
                    RunningProgram running;
                    p->Run();
                }
                done->set_value();
            });
        // The run must be parked in the read, not finished without it.
        const bool endedEarly = finished.wait_for(std::chrono::milliseconds(300)) ==
                                std::future_status::ready;
        p->PushInput("42");
        r.finished = finished.wait_for(std::chrono::milliseconds(kWatchdogMs)) ==
                     std::future_status::ready;
        r.out = syscallLines(testing::internal::GetCapturedStdout());
        if (!r.finished)
        {
            worker.detach();
            (void)r.processor.release();
            ADD_FAILURE() << "the run did not finish after the input arrived";
            return;
        }
        worker.join();
        EXPECT_FALSE(endedEarly) << "the run finished before any input was given";
        checkEcho(r);
    }
}

TEST_P(EcallSyscall, StopWakesARunWaitingForInput)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const Model model = GetParam();
    {
        std::unique_ptr<ProcessorBase> processor = load(model, dir, program(kEchoLines, true));
        ProcessorBase *p = processor.get();
        testing::internal::CaptureStdout();
        auto done = std::make_shared<std::promise<void>>();
        std::future<void> finished = done->get_future();
        std::thread worker(
            [p, done]()
            {
                {
                    RunningProgram running;
                    p->Run();
                }
                done->set_value();
            });
        const bool endedEarly = finished.wait_for(std::chrono::milliseconds(300)) ==
                                std::future_status::ready;
        const bool stopped = p->StopAndReset(2000);
        const bool left = finished.wait_for(std::chrono::milliseconds(kWatchdogMs)) ==
                          std::future_status::ready;
        const std::string out = syscallLines(testing::internal::GetCapturedStdout());
        if (!left)
        {
            worker.detach();
            (void)processor.release();
            ADD_FAILURE() << "the run did not leave Run() after the stop";
            return;
        }
        worker.join();
        EXPECT_FALSE(endedEarly) << "the run finished before it was stopped: " << out;
        EXPECT_TRUE(stopped) << "StopAndReset timed out";
        EXPECT_EQ(out, "VM_STDIN_START\n") << "parked in the read, and nothing after it";
    }
}

// ───────── a7 written immediately before the ecall ─────────

TEST_P(EcallSyscall, SeesTheA7WrittenImmediatelyBefore)
{
    // 93 is not in rvss's table: the ecall must report it and carry on.
    // Reading the stale 10 instead would exit before s0 is written.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<std::string> lines = {"li a0, 0",  "li a7, 10", "li a7, 93",
                                            "ecall",     "li s0, 77", "li a7, 10",
                                            "ecall",     "li s1, 88"};
    forEachPadding(GetParam(),
        [&](Model model, bool padded)
        {
            const Result r = runProgram(model, dir, program(lines, padded));
            ASSERT_TRUE(r.finished) << "the run did not finish";
            if (!padded && !owesUnpadded(model))
                return;
            EXPECT_EQ(r.err, "Unknown syscall number: 93\n");
            EXPECT_EQ(r.out, kExitLines);
            EXPECT_EQ(r.gpr(8), 77u) << "the unknown service carried on";
            EXPECT_EQ(r.gpr(9), 0u) << "the exit stopped the run";
        });
}

// ───────── exit drains what came before it, and nothing after ─────────

TEST_P(EcallSyscall, ExitKeepsAStoreJustBeforeItAndDropsOneAfter)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<std::string> lines = {
        "li a1, 0x10000000", "li t0, 1234", "li a0, 0",    "li a7, 10",
        "sd t0, 0(a1)",      "ecall",       "li s3, 9",    "sd s3, 8(a1)"};
    forEachPadding(GetParam(),
        [&](Model model, bool padded)
        {
            const Result r = runProgram(model, dir, program(lines, padded));
            ASSERT_TRUE(r.finished) << "the run did not finish";
            if (!padded && !owesUnpadded(model))
                return;
            EXPECT_EQ(r.out, kExitLines);
            EXPECT_EQ(r.dword(kDataBase), 1234u) << "the store before the exit";
            EXPECT_EQ(r.gpr(19), 0u) << "an instruction after the exit ran";
            EXPECT_EQ(r.dword(kDataBase + 8), 0u) << "a store after the exit ran";
        });
}

TEST_P(EcallSyscall, ExitLetsTheInstructionJustBeforeItWriteBack)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<std::string> lines = {"li a0, 0", "li a7, 10", "li s2, 5", "ecall",
                                            "li s3, 9"};
    forEachPadding(GetParam(),
        [&](Model model, bool padded)
        {
            const Result r = runProgram(model, dir, program(lines, padded));
            ASSERT_TRUE(r.finished) << "the run did not finish";
            if (!padded && !owesUnpadded(model))
                return;
            EXPECT_EQ(r.out, kExitLines);
            EXPECT_EQ(r.gpr(18), 5u) << "the write just before the exit was lost";
            EXPECT_EQ(r.gpr(19), 0u) << "an instruction after the exit ran";
        });
}

// ───────── exit reached by stepping, not running ─────────

TEST_P(EcallSyscall, SteppingOntoAnExitEndsTheProgramWithoutHanging)
{
    // ProcessorManager::step() calls StepAndNotify() on the UI thread, with
    // no Run() loop. The exit must end the stepping (StepAndNotify returns
    // false) and must not try to stop-and-reset from inside the step, which
    // holds step_mutex_ (known_issues 102).
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::vector<std::string> lines = {"li a0, 0", "li a7, 10", "li s2, 5", "ecall",
                                            "li s3, 9"};
    const Model model = GetParam();
    {
        std::unique_ptr<ProcessorBase> processor = load(model, dir, program(lines, true));
        ProcessorBase *p = processor.get();
        // Shared, not on this stack: an abandoned thread may still write it.
        auto steps = std::make_shared<std::atomic<int>>(0);
        testing::internal::CaptureStdout();
        const bool finished = finishesWithin(kWatchdogMs,
                                             [p, steps]()
                                             {
                                                 RunningProgram running;
                                                 while (*steps < 500 && p->StepAndNotify())
                                                     ++*steps;
                                             });
        const std::string out = syscallLines(testing::internal::GetCapturedStdout());
        if (!finished)
        {
            (void)processor.release();
            ADD_FAILURE() << "stepping onto the exit hung";
            return;
        }
        EXPECT_LT(steps->load(), 500) << "StepAndNotify never reported the end";
        EXPECT_EQ(out, kExitLines);
        EXPECT_EQ(p->registers_.ReadGpr(18), 5u);
        EXPECT_EQ(p->registers_.ReadGpr(19), 0u) << "an instruction after the exit ran";
    }
}
