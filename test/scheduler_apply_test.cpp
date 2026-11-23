/**
 * @file scheduler_apply_test.cpp
 * @brief The source-line apply layer (src/scheduler/reorder_apply.*) for the
 *        reordering scheduler (original author: Nandan Nihal,
 *        CS25BTECH11014): which lines move, which stay as barriers, and that
 *        the result re-assembles to the same instructions.
 */
#include "scheduler_test_support.h"

#include "scheduler/basic_block_scheduler.h"
#include "scheduler/reorder_apply.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Atlas;
using namespace Atlas::test_support::scheduler;

namespace
{
constexpr SchedulingOptions kForwarding{true, true};
constexpr SchedulingOptions kNoForwarding{false, true};

ApplyResult applyTo(const std::string &text, const SchedulingOptions &options = kForwarding)
{
    const AssembledProgram program = assembleText(text);
    return applySchedule(text, program, BasicBlockScheduler{}.schedule(program, options));
}

std::vector<std::string> linesOf(const std::string &text)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= text.size())
    {
        const size_t end = text.find('\n', start);
        out.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return out;
}

/// The load-use body, with `between` inserted after the add (the line the
/// scheduler wants to move addi x7 above).
std::string loadUse(const std::string &afterAdd = "", const std::string &prefix = "")
{
    return prefix + "addi x1, x0, 100\n"
                    "addi x2, x0, 20\n"
                    "sw   x2, 0(x1)\n"
                    "lw   x3, 0(x1)\n"
                    "add  x6, x3, x1\n" +
           afterAdd + "addi x7, x0, 1\n";
}
} // namespace

TEST(SchedulerApply, SourceLinesAreOneBased)
{
    // The mapping the whole layer rests on: line N of the text is
    // ICUnit::line_number N, comment and blank lines counted.
    const AssembledProgram program = assembleText(readDemo("load_use_original.s"));
    ASSERT_EQ(program.intermediate_code.size(), 6u);
    EXPECT_EQ(program.intermediate_code[0].first.getLineNumber(), 4u);
    EXPECT_EQ(program.intermediate_code[5].first.getLineNumber(), 9u);
}

TEST(SchedulerApply, LoadUseWithForwardingBecomesTheReorderedDemo)
{
    const std::string original = readDemo("load_use_original.s");
    const ApplyResult result   = applyTo(original);
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.movedLines, 2u);

    // Only the add and the addi x7 traded places; the comments are untouched.
    std::string expected = original;
    const std::string pair    = "add  x6, x3, x1\naddi x7, x0, 1";
    const std::string swapped = "addi x7, x0, 1\nadd  x6, x3, x1";
    expected.replace(expected.find(pair), pair.size(), swapped);
    EXPECT_EQ(result.text, expected);

    // Same machine code as the hand-written load_use_reordered.s.
    EXPECT_EQ(assembleText(result.text).text_buffer,
              assembleText(readDemo("load_use_reordered.s")).text_buffer);
}

TEST(SchedulerApply, LoadUseWithoutForwardingMovesTheAddiAboveTheStore)
{
    const ApplyResult result = applyTo(loadUse(), kNoForwarding);
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.text, "addi x1, x0, 100\n"
                           "addi x2, x0, 20\n"
                           "addi x7, x0, 1\n"
                           "sw   x2, 0(x1)\n"
                           "lw   x3, 0(x1)\n"
                           "add  x6, x3, x1\n");
}

TEST(SchedulerApply, TheInstructionMultisetSurvivesReassembly)
{
    for (const char *demo : {"load_use_original.s", "anti_dependency_safe.s", "memory_order_safe.s"})
        for (const SchedulingOptions &options : {kForwarding, kNoForwarding})
        {
            const std::string original = readDemo(demo);
            const ApplyResult result   = applyTo(original, options);
            EXPECT_TRUE(sameInstructionMultiset(assembleText(original), assembleText(result.text)))
                << demo;
        }
}

TEST(SchedulerApply, TheMultisetCheckNoticesADifferentInstruction)
{
    EXPECT_FALSE(sameInstructionMultiset(assembleText(loadUse()),
                                         assembleText(loadUse() + "addi x7, x0, 1\n")));
    std::string changed = loadUse();
    changed.replace(changed.find("x7, x0, 1"), 9, "x7, x0, 2");
    EXPECT_FALSE(sameInstructionMultiset(assembleText(loadUse()), assembleText(changed)));
}

TEST(SchedulerApply, SafetyDemosAreReturnedUnchanged)
{
    for (const char *demo : {"anti_dependency_safe.s", "memory_order_safe.s", "load_use_reordered.s"})
    {
        const std::string original = readDemo(demo);
        const ApplyResult result   = applyTo(original);
        EXPECT_FALSE(result.changed) << demo;
        EXPECT_EQ(result.movedLines, 0u) << demo;
        EXPECT_EQ(result.text, original) << demo;
    }
}

TEST(SchedulerApply, ACommentOnlyLineIsABarrier)
{
    const std::string text   = loadUse("# stays here, and nothing crosses it\n");
    const ApplyResult result = applyTo(text);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.text, text);
}

TEST(SchedulerApply, AMultiInstructionLineIsABarrier)
{
    // li with an immediate this large assembles to more than one
    // instruction, so it cannot move and nothing moves across it.
    const std::string text = loadUse("li   x9, 0x12345678\n");
    const AssembledProgram program = assembleText(text);
    unsigned int onLiLine = 0;
    for (const auto &[unit, _] : program.intermediate_code)
        if (unit.getLineNumber() == 6)
            ++onLiLine;
    ASSERT_GT(onLiLine, 1u) << "the fixture needs li to expand to several instructions";

    const ApplyResult result = applyTo(text);
    EXPECT_EQ(linesOf(result.text)[5], "li   x9, 0x12345678");
    EXPECT_EQ(linesOf(result.text)[6], "addi x7, x0, 1") << "addi x7 crossed the li line";
}

TEST(SchedulerApply, LaIsABarrier)
{
    const std::string prefix = ".data\nvalue: .word 5\n.text\n";
    const std::string text   = loadUse("la   x9, value\n", prefix);
    const ApplyResult result = applyTo(text);
    const std::vector<std::string> lines = linesOf(result.text);
    EXPECT_EQ(lines[0], ".data");
    EXPECT_EQ(lines[1], "value: .word 5");
    EXPECT_EQ(lines[2], ".text");
    EXPECT_EQ(lines[8], "la   x9, value");
    EXPECT_EQ(lines[9], "addi x7, x0, 1") << "addi x7 crossed the la line";
}

TEST(SchedulerApply, ALabelStaysWhereItIs)
{
    // A label starts a new basic block, and its line never moves.
    const std::string text   = loadUse("next:\n");
    const ApplyResult result = applyTo(text);
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(linesOf(result.text)[5], "next:");

    // The same with the label on the instruction's own line.
    const std::string sameLine = "addi x1, x0, 100\n"
                                 "addi x2, x0, 20\n"
                                 "sw   x2, 0(x1)\n"
                                 "lw   x3, 0(x1)\n"
                                 "add  x6, x3, x1\n"
                                 "next: addi x7, x0, 1\n";
    EXPECT_EQ(applyTo(sameLine).text, sameLine);
}

TEST(SchedulerApply, BlankLinesStayButDoNotBlock)
{
    const ApplyResult result = applyTo(loadUse("\n"));
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.text, "addi x1, x0, 100\n"
                           "addi x2, x0, 20\n"
                           "sw   x2, 0(x1)\n"
                           "lw   x3, 0(x1)\n"
                           "addi x7, x0, 1\n"
                           "\n"
                           "add  x6, x3, x1\n");
}

TEST(SchedulerApply, ATrailingCommentMovesWithItsInstruction)
{
    const std::string text = "addi x1, x0, 100\n"
                             "addi x2, x0, 20\n"
                             "sw   x2, 0(x1)\n"
                             "lw   x3, 0(x1)\n"
                             "add  x6, x3, x1   # the consumer\n"
                             "addi x7, x0, 1    # independent\n";
    const std::vector<std::string> lines = linesOf(applyTo(text).text);
    EXPECT_EQ(lines[4], "addi x7, x0, 1    # independent");
    EXPECT_EQ(lines[5], "add  x6, x3, x1   # the consumer");
}

TEST(SchedulerApply, LineEndingsAndTheFinalNewlineArePreserved)
{
    std::string crlf;
    for (const std::string &line : linesOf(loadUse()))
        if (!line.empty())
            crlf += line + "\r\n";
    const ApplyResult result = applyTo(crlf);
    ASSERT_TRUE(result.changed);
    EXPECT_NE(result.text.find("addi x7, x0, 1\r\nadd  x6, x3, x1\r\n"), std::string::npos);

    std::string noFinalNewline = loadUse();
    noFinalNewline.pop_back();
    const ApplyResult bare = applyTo(noFinalNewline);
    ASSERT_TRUE(bare.changed);
    EXPECT_NE(bare.text.back(), '\n');
    EXPECT_EQ(bare.text.size(), noFinalNewline.size());
}
