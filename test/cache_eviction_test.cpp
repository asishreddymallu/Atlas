// E8: the cache counts evictions - a fill that replaces a VALID line. Filling an invalid
// way is not an eviction. Expected values are worked by hand from the textbook definition.
#include <gtest/gtest.h>

#include "processor/cache/cache.h"
#include "processor/main_memory.h"

using namespace Atlas;

namespace
{
// 1 set x 2 ways x 16-byte lines: addresses 0, 16, 32, 48 all map to set 0.
Cache makeTwoWay(MainMemory &ram, ReplacementPolicy policy = ReplacementPolicy::LRU)
{
    return Cache(ram, 1, 16, 2, WritePolicy::WriteThrough, AllocationPolicy::NoWriteAllocate,
                 policy);
}
} // namespace

TEST(CacheEvictions, FillingAnInvalidWayIsNotAnEviction)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    cache.readWord(0);
    cache.readWord(16);
    EXPECT_EQ(cache.getMissCount(), 2u);
    EXPECT_EQ(cache.getEvictionCount(), 0u);
}

TEST(CacheEvictions, ReplacingAValidLineIsOne)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    for (uint64_t a : {0, 16, 0, 32, 16}) // LRU: m m h m(evicts 16) m(evicts 0)
        cache.readWord(a);
    EXPECT_EQ(cache.getHitCount(), 1u);
    EXPECT_EQ(cache.getMissCount(), 4u);
    EXPECT_EQ(cache.getEvictionCount(), 2u);
}

TEST(CacheEvictions, NoWriteAllocateStoreMissEvictsNothing)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    for (uint64_t a : {0, 16, 32, 48})
        cache.writeWord(a, 7);
    EXPECT_EQ(cache.getMissCount(), 4u);
    EXPECT_EQ(cache.getEvictionCount(), 0u);
}

TEST(CacheEvictions, ResetClearsTheCount)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    for (uint64_t a : {0, 16, 32})
        cache.readWord(a);
    ASSERT_EQ(cache.getEvictionCount(), 1u);
    cache.reset();
    EXPECT_EQ(cache.getEvictionCount(), 0u);
}

TEST(CacheEvictions, UndoAndRedoRestoreTheCount)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    cache.readWord(0);
    cache.readWord(16);
    cache.beginStep();
    cache.readWord(32); // evicts 0
    cache.commitStep();
    ASSERT_EQ(cache.getEvictionCount(), 1u);
    cache.undoStep();
    EXPECT_EQ(cache.getEvictionCount(), 0u);
    cache.redoStep();
    EXPECT_EQ(cache.getEvictionCount(), 1u);
}

TEST(CacheEvictions, StatsSignalCarriesTheCount)
{
    MainMemory ram;
    Cache cache(ram, 1, 16, 2);
    size_t seen = 999;
    QObject::connect(&cache, &Cache::cacheStatsUpdatedSignal,
                     [&](CacheStats s) { seen = s.evictionCount; });
    for (uint64_t a : {0, 16, 32})
        cache.readWord(a);
    EXPECT_EQ(seen, 1u);
}

TEST(CacheEvictions, FifoCountsTheSame)
{
    MainMemory ram;
    Cache cache = makeTwoWay(ram, ReplacementPolicy::FIFO);
    for (uint64_t a : {0, 16, 0, 32, 0}) // FIFO: m m h m(evicts 0) m(evicts 16)
        cache.readWord(a);
    EXPECT_EQ(cache.getMissCount(), 4u);
    EXPECT_EQ(cache.getEvictionCount(), 2u);
}
