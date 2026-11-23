/**
 * @file scheduler_gate_test.cpp
 * @brief The measured safety gate in front of "Suggest reordering"
 *        (src/scheduler_gate/), on top of the scheduler by Nandan Nihal
 *        (CS25BTECH11014).
 */
#include "scheduler_test_support.h"

#include "scheduler_gate/reorder_gate.h"

#include <gtest/gtest.h>

#include <string>

using namespace Atlas;
using namespace Atlas::test_support::scheduler;

namespace
{
ReorderSuggestion gate(const std::string &text, ProcessorType type,
                       unsigned int cap = kReorderCycleCap)
{
    setupVmStateDirectory();
    return suggestReordering(text, type, cap);
}
} // namespace

TEST(SchedulerGate, LoadUseIsOfferedWithForwardingAtTheMeasuredCycles)
{
    const ReorderSuggestion s = gate(readDemo("load_use_original.s"), ProcessorType::RV5Stage_H_F);
    ASSERT_EQ(s.status, ReorderStatus::Offered) << reorderStatusReason(s.status);
    EXPECT_EQ(s.originalCycles, 11u);
    EXPECT_EQ(s.suggestedCycles, 10u);
    EXPECT_EQ(s.movedLines, 2u);
    // The suggestion runs to the same cycles as the hand-written demo.
    EXPECT_EQ(assembleText(s.suggestedSource).text_buffer,
              assembleText(readDemo("load_use_reordered.s")).text_buffer);
}

TEST(SchedulerGate, LoadUseIsOfferedWithoutForwardingAtTheMeasuredCycles)
{
    const ReorderSuggestion s = gate(readDemo("load_use_original.s"), ProcessorType::RV5Stage_H_NF);
    ASSERT_EQ(s.status, ReorderStatus::Offered) << reorderStatusReason(s.status);
    EXPECT_EQ(s.originalCycles, 14u);
    // Without forwarding the scheduler moves addi x7 above the sw (not
    // between the lw and the add, as load_use_reordered.s does). Measured,
    // it saves the same cycle: 14 -> 13.
    EXPECT_EQ(s.suggestedCycles, 13u);
}

TEST(SchedulerGate, SafetyDemosOfferNothing)
{
    for (const char *demo : {"anti_dependency_safe.s", "memory_order_safe.s"})
        for (ProcessorType type : {ProcessorType::RV5Stage_H_F, ProcessorType::RV5Stage_H_NF})
        {
            const ReorderSuggestion s = gate(readDemo(demo), type);
            EXPECT_EQ(s.status, ReorderStatus::NoChange) << demo;
            EXPECT_FALSE(s.offered()) << demo;
        }
}

TEST(SchedulerGate, AnAlreadyReorderedProgramIsNotOfferedAgain)
{
    // With forwarding the scheduler leaves load_use_reordered.s alone.
    const ReorderSuggestion withForwarding =
        gate(readDemo("load_use_reordered.s"), ProcessorType::RV5Stage_H_F);
    EXPECT_EQ(withForwarding.status, ReorderStatus::NoChange);

    // Without forwarding its estimate still prefers addi x7 above the sw.
    // The gate measures that suggestion, finds it no faster, and offers
    // nothing: the scheduler's estimate alone never reaches the user.
    const ReorderSuggestion withoutForwarding =
        gate(readDemo("load_use_reordered.s"), ProcessorType::RV5Stage_H_NF);
    EXPECT_EQ(withoutForwarding.status, ReorderStatus::NotFaster);
    EXPECT_FALSE(withoutForwarding.offered());
    EXPECT_EQ(withoutForwarding.originalCycles, 13u);
    EXPECT_EQ(withoutForwarding.suggestedCycles, 13u);
}

TEST(SchedulerGate, NoSuggestionWithoutHazardDetectionOrOnTheSingleCycleModel)
{
    for (ProcessorType type : {ProcessorType::RVSS, ProcessorType::RV5Stage_NH_F,
                               ProcessorType::RV5Stage_NH_NF})
    {
        EXPECT_FALSE(reorderingSupported(type));
        const ReorderSuggestion s = gate(readDemo("load_use_original.s"), type);
        EXPECT_EQ(s.status, ReorderStatus::UnsupportedProcessor);
        EXPECT_FALSE(s.offered());
        EXPECT_TRUE(s.suggestedSource.empty());
    }
    EXPECT_TRUE(reorderingSupported(ProcessorType::RV5Stage_H_F));
    EXPECT_TRUE(reorderingSupported(ProcessorType::RV5Stage_H_NF));
}

TEST(SchedulerGate, AProgramThatNeverHaltsIsNotOffered)
{
    // The first block can be reordered, then the program spins forever.
    const std::string text = readDemo("load_use_original.s") + "spin:\nj spin\n";
    const ReorderSuggestion s = gate(text, ProcessorType::RV5Stage_H_F, 2000);
    EXPECT_EQ(s.status, ReorderStatus::NotHalted);
    EXPECT_FALSE(s.offered());
    EXPECT_GE(s.originalCycles, 2000u);
}

TEST(SchedulerGate, AProgramThatDoesNotAssembleIsNotOffered)
{
    const ReorderSuggestion s = gate("addi x1, x0\nbogus x1\n", ProcessorType::RV5Stage_H_F);
    EXPECT_EQ(s.status, ReorderStatus::AssembleFailed);
    EXPECT_FALSE(s.offered());
}

TEST(SchedulerGate, EveryStatusHasAReason)
{
    for (ReorderStatus status :
         {ReorderStatus::Offered, ReorderStatus::UnsupportedProcessor, ReorderStatus::CustomPseudo,
          ReorderStatus::AssembleFailed, ReorderStatus::NoChange, ReorderStatus::NotHalted,
          ReorderStatus::StateDiffers, ReorderStatus::MultisetDiffers, ReorderStatus::NotFaster})
        EXPECT_FALSE(reorderStatusReason(status).empty());
}
