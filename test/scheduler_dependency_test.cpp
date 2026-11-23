/**
 * @file scheduler_dependency_test.cpp
 * @brief The dependency edges and the order the reordering scheduler
 *        (original author: Nandan Nihal, CS25BTECH11014) builds for the four
 *        Phase 1 demos in examples/scheduler/.
 *
 * Indices are block-local (positions in BasicBlockSchedule::
 * originalInstructionIndices). Every demo is one basic block: no labels and
 * no control transfer.
 */
#include "scheduler_test_support.h"

#include "scheduler/basic_block_scheduler.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <tuple>
#include <vector>

using namespace Atlas;
using namespace Atlas::test_support::scheduler;

namespace
{
using Edge = std::tuple<size_t, size_t, DependencyKind, unsigned int>;

constexpr SchedulingOptions kForwarding{true, true};
constexpr SchedulingOptions kNoForwarding{false, true};

const BasicBlockSchedule &onlyBlock(const SchedulingResult &result)
{
    static const BasicBlockSchedule kNone;
    EXPECT_EQ(result.blocks.size(), 1u) << "expected the demo to be one basic block";
    return result.blocks.empty() ? kNone : result.blocks.front();
}

std::vector<Edge> edgesOf(const BasicBlockSchedule &block)
{
    std::vector<Edge> out;
    for (const DependencyEdge &e : block.dependencies)
        out.emplace_back(e.producer, e.consumer, e.kind, e.latency);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Edge> sorted(std::vector<Edge> edges)
{
    std::sort(edges.begin(), edges.end());
    return edges;
}

/// The scheduled order as block-local positions.
std::vector<size_t> scheduledOrder(const BasicBlockSchedule &block)
{
    std::vector<size_t> out;
    for (size_t programIndex : block.scheduledInstructionIndices)
    {
        const auto it = std::find(block.originalInstructionIndices.begin(),
                                  block.originalInstructionIndices.end(), programIndex);
        out.push_back(static_cast<size_t>(it - block.originalInstructionIndices.begin()));
    }
    return out;
}

SchedulingResult scheduleDemo(const std::string &name, const SchedulingOptions &options)
{
    return BasicBlockScheduler{}.schedule(assembleText(readDemo(name)), options);
}

constexpr auto RAW = DependencyKind::ReadAfterWrite;
constexpr auto WAR = DependencyKind::WriteAfterRead;
constexpr auto MEM = DependencyKind::MemoryOrder;
} // namespace

// 0 addi x1,x0,100  1 addi x2,x0,20  2 sw x2,0(x1)  3 lw x3,0(x1)
// 4 add x6,x3,x1    5 addi x7,x0,1
TEST(SchedulerDependency, LoadUseOriginalEdgesWithForwarding)
{
    const SchedulingResult result = scheduleDemo("load_use_original.s", kForwarding);
    const BasicBlockSchedule &block = onlyBlock(result);
    ASSERT_EQ(block.originalInstructionIndices.size(), 6u);
    EXPECT_EQ(edgesOf(block), sorted({
                                  {0, 2, RAW, 1}, // x1, the store's base
                                  {1, 2, RAW, 1}, // x2, the stored value
                                  {0, 3, RAW, 1}, // x1, the load's base
                                  {0, 4, RAW, 1}, // x1 into the add
                                  {2, 3, MEM, 1}, // the store stays before the load
                                  {3, 4, RAW, 2}, // the load-use pair: a load's value is a cycle late
                              }));
}

TEST(SchedulerDependency, LoadUseOriginalEdgesWithoutForwarding)
{
    const SchedulingResult result = scheduleDemo("load_use_original.s", kNoForwarding);
    const BasicBlockSchedule &block = onlyBlock(result);
    // Without forwarding every RAW waits for writeback.
    EXPECT_EQ(edgesOf(block), sorted({
                                  {0, 2, RAW, 3},
                                  {1, 2, RAW, 3},
                                  {0, 3, RAW, 3},
                                  {0, 4, RAW, 3},
                                  {2, 3, MEM, 1},
                                  {3, 4, RAW, 3},
                              }));
}

TEST(SchedulerDependency, LoadUseOriginalMovesTheIndependentAddiIntoTheGap)
{
    // With forwarding: exactly load_use_reordered.s - addi x7 between the lw
    // and its consumer.
    const SchedulingResult forwarding = scheduleDemo("load_use_original.s", kForwarding);
    EXPECT_EQ(scheduledOrder(onlyBlock(forwarding)), (std::vector<size_t>{0, 1, 2, 3, 5, 4}));
    EXPECT_TRUE(forwarding.changed());

    // Without forwarding every RAW is 3 cycles, so at cycle 2 the sw is not
    // ready yet and addi x7 is the only instruction that can issue: it moves
    // above the store instead. Still legal - it depends on nothing.
    const SchedulingResult noForwarding = scheduleDemo("load_use_original.s", kNoForwarding);
    EXPECT_EQ(scheduledOrder(onlyBlock(noForwarding)), (std::vector<size_t>{0, 1, 5, 2, 3, 4}));
    EXPECT_TRUE(noForwarding.changed());
}

// 0 addi x1,x0,100  1 lw x5,0(x1)  2 add x6,x5,x2  3 addi x2,x3,1
TEST(SchedulerDependency, AntiDependencyEdges)
{
    const SchedulingResult result = scheduleDemo("anti_dependency_safe.s", kForwarding);
    EXPECT_EQ(edgesOf(onlyBlock(result)), sorted({
                                              {0, 1, RAW, 1},
                                              {1, 2, RAW, 2},
                                              {2, 3, WAR, 1}, // add reads the OLD x2
                                          }));
}

TEST(SchedulerDependency, AntiDependencySafeKeepsTheOrder)
{
    for (const SchedulingOptions &options : {kForwarding, kNoForwarding})
    {
        const SchedulingResult result = scheduleDemo("anti_dependency_safe.s", options);
        // addi x2 would fill the load-use gap, but the WAR edge pins it
        // after the add.
        EXPECT_EQ(scheduledOrder(onlyBlock(result)), (std::vector<size_t>{0, 1, 2, 3}))
            << "forwarding=" << options.forwardingEnabled;
        EXPECT_FALSE(result.changed());
    }
}

// 0 lw x5,0(x1)  1 sw x2,0(x1)  2 add x6,x5,x3
TEST(SchedulerDependency, MemoryOrderEdges)
{
    const SchedulingResult result = scheduleDemo("memory_order_safe.s", kForwarding);
    EXPECT_EQ(edgesOf(onlyBlock(result)), sorted({
                                              {0, 1, MEM, 1}, // load before store
                                              {0, 2, RAW, 2},
                                          }));
}

TEST(SchedulerDependency, MemoryOrderSafeKeepsTheOrder)
{
    for (const SchedulingOptions &options : {kForwarding, kNoForwarding})
    {
        const SchedulingResult result = scheduleDemo("memory_order_safe.s", options);
        const std::vector<size_t> order = scheduledOrder(onlyBlock(result));
        EXPECT_EQ(order, (std::vector<size_t>{0, 1, 2}))
            << "forwarding=" << options.forwardingEnabled;
        // Whatever else happens, the store never moves above the load.
        const auto load  = std::find(order.begin(), order.end(), 0u);
        const auto store = std::find(order.begin(), order.end(), 1u);
        EXPECT_LT(load, store);
    }
}

TEST(SchedulerDependency, ReorderedDemoIsAlreadyOptimal)
{
    const SchedulingResult result = scheduleDemo("load_use_reordered.s", kForwarding);
    EXPECT_EQ(scheduledOrder(onlyBlock(result)), (std::vector<size_t>{0, 1, 2, 3, 4, 5}));
    EXPECT_FALSE(result.changed());
}
