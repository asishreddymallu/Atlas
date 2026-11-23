/**
 * @file cache_reference_check_test.cpp
 * @brief The live cache view checked against an independent reference
 *        model (cache_reference_model.h), access by access.
 *
 * The product supplies only the address stream (each record's address and
 * read/write). Every decision - set, tag, hit or miss, which tag was
 * evicted - is recomputed by the reference model and compared with what the
 * live view's observer was handed, with its 16-entry log, with its shadow
 * snapshot as the accesses left it (no resync), and with the final
 * counters.
 */
#include "cache_lab_test_support.h"
#include "cache_reference_model.h"

#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

using namespace Atlas;
using namespace Atlas::test_support;
using namespace Atlas::test_support::cache_lab;

namespace
{
/// "" when every record matches the model fed the same addresses,
/// otherwise the first difference.
std::string compareRecords(const std::vector<CacheAccessRecord> &records, ReferenceCache &model)
{
    for (size_t i = 0; i < records.size(); ++i)
    {
        const CacheAccessRecord &r = records[i];
        const ReferenceCache::Outcome want = model.access(r.address, r.isWrite);
        const std::string at = "access " + std::to_string(i) + " (0x" +
                               QString::number(r.address, 16).toStdString() + "): ";
        if (r.setIndex != want.set)
            return at + "set " + std::to_string(r.setIndex) + " vs " + std::to_string(want.set);
        if (r.tag != want.tag)
            return at + "tag " + std::to_string(r.tag) + " vs " + std::to_string(want.tag);
        if (r.hit != want.hit)
            return at + (r.hit ? "hit" : "miss") + " vs " + (want.hit ? "hit" : "miss");
        if (r.evicted != want.evicted || (want.evicted && r.evictedTag != want.evictedTag))
            return at + "evicted " + (r.evicted ? std::to_string(r.evictedTag) : "none") + " vs " +
                   (want.evicted ? std::to_string(want.evictedTag) : "none");
    }
    return "";
}

/// "" when the snapshot holds what the model holds, way by way.
std::string compareSnapshot(const CacheSnapshot &snapshot, const ReferenceCache &model)
{
    for (size_t set = 0; set < snapshot.sets; ++set)
        for (size_t way = 0; way < snapshot.ways; ++way)
        {
            const CacheLineSnapshot  &have = snapshot.at(set, way);
            const ReferenceCache::Way &want = model.way(set, way);
            if (have.valid != want.valid || (want.valid && have.tag != want.tag))
                return "set " + std::to_string(set) + " way " + std::to_string(way);
        }
    return "";
}

struct Case
{
    const char *program;
    const char *config;
};

class CacheReferenceCheck : public ::testing::TestWithParam<Case>
{
};

CacheConfig configNamed(const std::string &name)
{
    return name == "dm256" ? dm256() : twoWay256Lru();
}
} // namespace

TEST_P(CacheReferenceCheck, TheLiveViewAgreesWithTheReferenceModel)
{
    const Case       &c   = GetParam();
    const CacheConfig cfg = configNamed(c.config);
    const LabRun run = runOnRvss(c.program, cfg, true, /*resyncAtEnd*/ false);
    ASSERT_FALSE(run.records.empty());

    ReferenceCache model(cfg.lineCount, cfg.wayCount, cfg.lineSizeInBytes,
                         cfg.allocationPolicy == AllocationPolicy::WriteAllocate);
    EXPECT_EQ(compareRecords(run.records, model), "");

    // Final counters: the product's, the model's, and what the records say.
    EXPECT_EQ(run.l1()->getHitCount(), model.hits);
    EXPECT_EQ(run.l1()->getMissCount(), model.misses);
    EXPECT_EQ(run.l1()->getEvictionCount(), model.evictions);
    EXPECT_EQ(run.records.size(), model.hits + model.misses);

    // The observer's state, kept only from the accesses (no resync).
    const CacheLiveObserver::State state = run.observer->state();
    EXPECT_EQ(compareSnapshot(state.snapshot, model), "");
    ASSERT_EQ(state.log.size(), CacheLiveObserver::kLogSize);
    for (size_t i = 0; i < state.log.size(); ++i)
    {
        const CacheAccessRecord &logged = state.log[i];
        const CacheAccessRecord &full   = run.records[run.records.size() - state.log.size() + i];
        EXPECT_EQ(logged.sequence, full.sequence) << i;
        EXPECT_EQ(CacheLiveObserver::describe(logged), CacheLiveObserver::describe(full)) << i;
    }
}

INSTANTIATE_TEST_SUITE_P(Programs, CacheReferenceCheck,
                         ::testing::Values(Case{"stride_1", "dm256"}, Case{"stride_16", "dm256"},
                                           Case{"temporal_repeat", "dm256"},
                                           Case{"stride_1", "2w256lru"}, Case{"stride_16", "2w256lru"},
                                           Case{"temporal_repeat", "2w256lru"}),
                         [](const ::testing::TestParamInfo<Case> &info)
                         { return std::string(info.param.program) + "_" + info.param.config; });

TEST(CacheReferenceCheck, NegativeControlAWrongTagInTheLogIsCaught)
{
    const LabRun run = runOnRvss("stride_16", dm256(), true, false);
    std::vector<CacheAccessRecord> broken = run.records;
    ASSERT_GT(broken.size(), 10u);
    broken[10].tag += 1; // one wrong tag, as a buggy observer might report

    ReferenceCache model(16, 1, 16, false);
    const std::string difference = compareRecords(broken, model);
    EXPECT_EQ(difference.rfind("access 10 ", 0), 0u) << difference;
}

TEST(CacheReferenceCheck, NegativeControlAWrongTagInTheSnapshotIsCaught)
{
    const LabRun run = runOnRvss("stride_1", dm256(), true, false);
    ReferenceCache model(16, 1, 16, false);
    ASSERT_EQ(compareRecords(run.records, model), "");

    CacheSnapshot broken = run.observer->state().snapshot;
    ASSERT_TRUE(broken.at(3, 0).valid);
    broken.lines[3].tag ^= 0x1; // set 3, way 0
    EXPECT_EQ(compareSnapshot(broken, model), "set 3 way 0");
}

TEST(CacheReferenceCheck, NegativeControlAWrongHitIsCaught)
{
    const LabRun run = runOnRvss("temporal_repeat", dm256(), true, false);
    std::vector<CacheAccessRecord> broken = run.records;
    broken.back().hit = !broken.back().hit;
    ReferenceCache model(16, 1, 16, false);
    EXPECT_NE(compareRecords(broken, model), "");
}
