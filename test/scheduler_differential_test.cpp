/**
 * @file scheduler_differential_test.cpp
 * @brief Differential checks for the reordering scheduler (original author:
 *        Nandan Nihal, CS25BTECH11014): the original and the reordered
 *        program run to halt on the five-stage processors WITH hazard
 *        detection and must leave identical registers and memory, and the
 *        reordered one must take fewer MEASURED cycles.
 *
 * The no-hazard-detection models are deliberately not compared: there an
 * unresolved dependency reads a stale operand by design, so reordering can
 * legitimately change the result (examples/scheduler/README.md).
 */
#include "scheduler_test_support.h"

#include "scheduler/basic_block_scheduler.h"

#include <gtest/gtest.h>

#include <string>

using namespace Atlas;
using namespace Atlas::test_support;
using namespace Atlas::test_support::scheduler;

namespace
{
/// The original program with `first` and `second` (whole lines) swapped.
std::string swapLines(std::string text, const std::string &first, const std::string &second)
{
    const size_t a = text.find(first);
    const size_t b = text.find(second);
    EXPECT_NE(a, std::string::npos);
    EXPECT_NE(b, std::string::npos);
    EXPECT_LT(a, b);
    text.replace(b, second.size(), first);
    text.replace(a, first.size(), second);
    return text;
}
} // namespace

TEST(SchedulerDifferential, LoadUseReorderedMatchesOriginalWithForwarding)
{
    FinalState original  = runToHalt(Model::Rv5HF, readDemo("load_use_original.s"));
    FinalState reordered = runToHalt(Model::Rv5HF, readDemo("load_use_reordered.s"));
    EXPECT_EQ(compareFinalState(original, reordered), "");
    EXPECT_EQ(original.x[6], 120u);
    EXPECT_EQ(original.x[7], 1u);
    EXPECT_TRUE(original.written.contains(100)) << "the sw to address 100 was not seen";

    // README claim, measured on Atlas: h_f 11 -> 10.
    EXPECT_EQ(original.cycles, 11u);
    EXPECT_EQ(reordered.cycles, 10u);
}

TEST(SchedulerDifferential, LoadUseReorderedMatchesOriginalWithoutForwarding)
{
    FinalState original  = runToHalt(Model::Rv5HNf, readDemo("load_use_original.s"));
    FinalState reordered = runToHalt(Model::Rv5HNf, readDemo("load_use_reordered.s"));
    EXPECT_EQ(compareFinalState(original, reordered), "");
    EXPECT_EQ(original.x[6], 120u);

    // README claim, measured on Atlas: h_nf 14 -> 13.
    EXPECT_EQ(original.cycles, 14u);
    EXPECT_EQ(reordered.cycles, 13u);
}

TEST(SchedulerDifferential, SafetyDemosMatchThemselvesOnBothHazardModels)
{
    // The scheduler keeps their order (scheduler_dependency_test), so the
    // "reordered" program is the original; the check still has to hold.
    for (const char *demo : {"anti_dependency_safe.s", "memory_order_safe.s"})
        for (Model model : {Model::Rv5HF, Model::Rv5HNf})
        {
            FinalState a = runToHalt(model, readDemo(demo));
            FinalState b = runToHalt(model, readDemo(demo));
            EXPECT_EQ(compareFinalState(a, b), "") << demo << " on " << name(model);
        }
}

TEST(SchedulerDifferential, NegativeControlSwappingADependentPairIsCaught)
{
    // lw x3 then add x6, x3, x1 is a true dependency. Swapping them by hand
    // makes the add read x3 before the load writes it, so x6 = 0 + 100
    // instead of 20 + 100. The differential check must see that.
    const std::string broken =
        swapLines(readDemo("load_use_original.s"), "lw   x3, 0(x1)", "add  x6, x3, x1");
    for (Model model : {Model::Rv5HF, Model::Rv5HNf})
    {
        FinalState original = runToHalt(model, readDemo("load_use_original.s"));
        FinalState swapped  = runToHalt(model, broken);
        ASSERT_TRUE(swapped.halted);
        EXPECT_EQ(swapped.x[6], 100u) << name(model);
        EXPECT_NE(compareFinalState(original, swapped), "") << name(model);
    }
}

TEST(SchedulerDifferential, NegativeControlMovingAStoreAboveItsValueIsCaught)
{
    // A memory-only difference: moved above the addi that makes its value,
    // the store writes 0 instead of 20. Both runs end with x2 = 0, so the
    // registers agree and only the memory comparison can catch it.
    const std::string text = "addi x1, x0, 100\n"
                             "addi x2, x0, 20\n"
                             "sw   x2, 0(x1)\n"
                             "addi x2, x0, 0\n";
    const std::string broken = "addi x1, x0, 100\n"
                               "sw   x2, 0(x1)\n"
                               "addi x2, x0, 20\n"
                               "addi x2, x0, 0\n";
    FinalState a = runToHalt(Model::Rv5HF, text);
    FinalState b = runToHalt(Model::Rv5HF, broken);
    const std::string difference = compareFinalState(a, b);
    EXPECT_EQ(difference.rfind("memory[", 0), 0u) << difference;
}
