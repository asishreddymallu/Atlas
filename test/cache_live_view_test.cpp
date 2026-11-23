/**
 * @file cache_live_view_test.cpp
 * @brief The live cache-line view: the cache's passive last-access record
 *        and const snapshots, the observer, the set x way model and the
 *        view, and that observing changes no counter.
 */
#include "cache_lab_test_support.h"

#include "processor/main_memory.h"
#include "ui/cache_tab/cache_live_view.h"
#include "ui/cache_tab/cache_set_model.h"
#include "ui/main_window/main_window.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/theme/theme_manager.h"

#include <QHeaderView>
#include <QTableView>

#include <gtest/gtest.h>

#include <memory>

using namespace Atlas;
using namespace Atlas::test_support::cache_lab;

namespace
{
/// 4 sets x 2 ways x 16 B, write back + write allocate, LRU, over plain memory.
struct SmallCache
{
    MainMemory ram;
    Cache      cache{ram, 4, 16, 2, WritePolicy::WriteBack, AllocationPolicy::WriteAllocate,
                ReplacementPolicy::LRU};
};
// Addresses in set 1 with tags 1, 2, 3: offset 4 bits, set 2 bits.
constexpr uint64_t kTag1 = (1u << 6) | (1u << 4);
constexpr uint64_t kTag2 = (2u << 6) | (1u << 4);
constexpr uint64_t kTag3 = (3u << 6) | (1u << 4);
} // namespace

TEST(CacheLiveView, TheRecordStartsEmpty)
{
    SmallCache s;
    EXPECT_EQ(s.cache.lastAccess().sequence, 0u);
    EXPECT_EQ(CacheLiveObserver::describe(s.cache.lastAccess()), "No access yet");
}

TEST(CacheLiveView, AMissThenAHitAreRecorded)
{
    SmallCache s;
    (void)s.cache.readWord(kTag1);
    CacheAccessRecord r = s.cache.lastAccess();
    EXPECT_EQ(r.sequence, 1u);
    EXPECT_EQ(r.address, kTag1);
    EXPECT_EQ(r.setIndex, 1u);
    EXPECT_EQ(r.tag, 1u);
    EXPECT_EQ(r.size, 4u);
    EXPECT_FALSE(r.isWrite);
    EXPECT_FALSE(r.hit);
    EXPECT_FALSE(r.evicted) << "filling an empty way is not an eviction";

    (void)s.cache.readWord(kTag1 + 4);
    r = s.cache.lastAccess();
    EXPECT_EQ(r.sequence, 2u);
    EXPECT_TRUE(r.hit);
    EXPECT_EQ(CacheLiveObserver::describe(r), "READ 0x00000054 -> set 1, tag 0x1: HIT");
}

TEST(CacheLiveView, AnEvictionRecordsTheReplacedTag)
{
    SmallCache s;
    (void)s.cache.readWord(kTag1);
    (void)s.cache.readWord(kTag2); // set 1 is now full
    (void)s.cache.readWord(kTag1); // tag 1 most recent, so LRU replaces tag 2
    s.cache.writeWord(kTag3, 7);
    const CacheAccessRecord r = s.cache.lastAccess();
    EXPECT_TRUE(r.isWrite);
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.evicted);
    EXPECT_EQ(r.evictedTag, 2u);
    EXPECT_EQ(CacheLiveObserver::describe(r),
              "WRITE 0x000000d0 -> set 1, tag 0x3: MISS, evicted tag 0x2");
}

TEST(CacheLiveView, TheSnapshotMatchesTheLines)
{
    SmallCache s;
    s.cache.writeWord(kTag1, 1); // write back: dirty
    (void)s.cache.readWord(kTag2);
    const CacheSnapshot snap = s.cache.snapshot();
    ASSERT_EQ(snap.sets, 4u);
    ASSERT_EQ(snap.ways, 2u);
    EXPECT_EQ(snap.lineBytes, 16u);
    ASSERT_EQ(snap.lines.size(), 8u);
    for (size_t set = 0; set < 4; ++set)
        for (size_t way = 0; way < 2; ++way)
        {
            const CacheLine &line = s.cache.getCacheLine(set, way);
            EXPECT_EQ(snap.at(set, way).valid, line.valid);
            EXPECT_EQ(snap.at(set, way).dirty, line.dirty);
            EXPECT_EQ(snap.at(set, way).tag, line.tag);
            EXPECT_EQ(snap.at(set, way).lastAccess, line.lastAccess);
        }
    EXPECT_TRUE(snap.at(1, 0).dirty);
    EXPECT_EQ(s.cache.setSnapshot(1).size(), 2u);
    EXPECT_TRUE(s.cache.setSnapshot(99).empty());
}

TEST(CacheLiveView, TheObserverLogsTheLastSixteenAndFollowsTheTouchedSet)
{
    SmallCache        s;
    CacheLiveObserver observer;
    observer.attach(&s.cache);
    for (uint64_t i = 0; i < 20; ++i)
        (void)s.cache.readByte(i * 16); // 20 lines through 4 sets
    const CacheLiveObserver::State st = observer.state();
    ASSERT_EQ(st.log.size(), CacheLiveObserver::kLogSize);
    EXPECT_EQ(st.log.front().sequence, 5u);
    EXPECT_EQ(st.log.back().sequence, 20u);
    // The shadow was kept current from the accesses alone, with no resync.
    const CacheSnapshot actual = s.cache.snapshot();
    for (size_t i = 0; i < actual.lines.size(); ++i)
    {
        EXPECT_EQ(st.snapshot.lines[i].valid, actual.lines[i].valid) << i;
        EXPECT_EQ(st.snapshot.lines[i].tag, actual.lines[i].tag) << i;
    }
}

TEST(CacheLiveView, TheModelHasOneRowPerSetAndOneColumnPerWay)
{
    SmallCache s;
    s.cache.writeWord(kTag1, 1);
    CacheSetModel model;
    model.setState(s.cache.snapshot(), s.cache.lastAccess());
    EXPECT_EQ(model.rowCount(), 4);
    EXPECT_EQ(model.columnCount(), 2);
    EXPECT_EQ(model.headerData(3, Qt::Vertical).toString(), "Set 3");
    EXPECT_EQ(model.headerData(1, Qt::Horizontal).toString(), "Way 1");
    EXPECT_EQ(model.data(model.index(1, 0)).toString(), "0x1 V D");
    EXPECT_EQ(model.data(model.index(0, 0)).toString(), "-");
    EXPECT_TRUE(model.data(model.index(1, 0), Qt::ToolTipRole).toString().contains("LRU order"));
}

TEST(CacheLiveView, TheTouchedLineUsesTheThemeHitAndMissColours)
{
    SmallCache s;
    CacheSetModel model;
    (void)s.cache.readWord(kTag1);
    model.setState(s.cache.snapshot(), s.cache.lastAccess());
    ASSERT_EQ(model.touchedIndex(), model.index(1, 0));
    EXPECT_EQ(model.data(model.touchedIndex(), Qt::BackgroundRole).value<QColor>(),
              ThemeManager::getInstance().getCacheMissColor());
    EXPECT_FALSE(model.data(model.index(1, 1), Qt::BackgroundRole).isValid())
        << "only the touched line is painted";

    (void)s.cache.readWord(kTag1);
    model.setState(s.cache.snapshot(), s.cache.lastAccess());
    EXPECT_EQ(model.data(model.touchedIndex(), Qt::BackgroundRole).value<QColor>(),
              ThemeManager::getInstance().getCacheHitColor());
}

TEST(CacheLiveView, AStoreThatAllocatesNothingTouchesNoLine)
{
    MainMemory ram;
    Cache      cache(ram, 4, 16, 1, WritePolicy::WriteThrough, AllocationPolicy::NoWriteAllocate,
                     ReplacementPolicy::LRU);
    cache.writeWord(kTag1, 3);
    CacheSetModel model;
    model.setState(cache.snapshot(), cache.lastAccess());
    EXPECT_FALSE(model.touchedIndex().isValid());
    EXPECT_FALSE(cache.lastAccess().hit);
}

TEST(CacheLiveView, ALargeCacheStaysVirtualised)
{
    // 4096 sets x 4 ways: the view must not size every row to its contents
    // or build a widget per cell.
    MainMemory ram;
    Cache cache(ram, 4096, 16, 4, WritePolicy::WriteThrough, AllocationPolicy::NoWriteAllocate,
                ReplacementPolicy::LRU);
    CacheLiveView view;
    view.resize(600, 400);
    CacheLiveObserver observer;
    observer.attach(&cache);
    view.showState(observer.state());
    EXPECT_EQ(view.model()->rowCount(), 4096);
    EXPECT_EQ(view.table()->verticalHeader()->sectionResizeMode(0), QHeaderView::Fixed);
    EXPECT_TRUE(view.table()->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly).size() <
                50)
        << "per-cell widgets";
}

TEST(CacheLiveView, ObservingChangesNoCounter)
{
    // dm256 stride_1 is 768/256/240 (cache_lab_cases.inc, the E8 oracle).
    const LabRun plain    = runOnRvss("stride_1", dm256(), false);
    const LabRun observed = runOnRvss("stride_1", dm256(), true);
    for (const LabRun *run : {&plain, &observed})
    {
        EXPECT_EQ(run->l1()->getHitCount(), 768u);
        EXPECT_EQ(run->l1()->getMissCount(), 256u);
        EXPECT_EQ(run->l1()->getEvictionCount(), 240u);
    }
    EXPECT_EQ(observed.records.size(), 1024u) << "one record per access";
}

TEST(CacheLiveView, EachSubTabHasALiveViewAboveTheDataTable)
{
    MainWindow window;
    auto *tab = window.findChild<CacheTab *>();
    ASSERT_NE(tab, nullptr);
    for (CacheTab::CacheLevel level : {CacheTab::Instruction, CacheTab::L1, CacheTab::L2})
    {
        ASSERT_NE(tab->liveView(level), nullptr);
        ASSERT_NE(tab->liveObserver(level), nullptr);
        EXPECT_GT(tab->liveView(level)->model()->rowCount(), 0)
            << "the view shows the engine's geometry from the start";
    }
    // The existing per-line tables are still there, by name.
    for (const char *name : {"L1tableView", "L2tableView", "InstructiontableView"})
        EXPECT_NE(tab->findChild<QTableView *>(name), nullptr) << name;
}
