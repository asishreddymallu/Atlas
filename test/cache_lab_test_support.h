/**
 * @file cache_lab_test_support.h
 * @brief Running a cache-lab program headless on RVSS with a given L1, and
 *        collecting every access record the live-view observer sees.
 *
 * The data-cache counts are the same on all five models (E8,
 * cache_lab_test.cpp), so RVSS - fewest cycles - is used, as the Cache tab
 * GUI tests do.
 */
#ifndef ATLAS_TEST_CACHE_LAB_TEST_SUPPORT_H
#define ATLAS_TEST_CACHE_LAB_TEST_SUPPORT_H

#include "assembler/assembler.h"
#include "processor/cache/cache.h"
#include "processor/rvss/rvss_processor.h"
#include "ui/cache_tab/cache_live_observer.h"
#include "utils/utils.h"

#include <QObject>

#include <memory>
#include <string>
#include <vector>

namespace Atlas::test_support::cache_lab
{
inline std::string examplePath(const std::string &program)
{
    return "../examples/cache_lab/" + program + ".s";
}

/// dm256: 16 sets x 1 way x 16 B, write through, no write allocate, LRU.
inline CacheConfig dm256()
{
    return {16, 16, 1, WritePolicy::WriteThrough, AllocationPolicy::NoWriteAllocate,
            ReplacementPolicy::LRU};
}
/// 2w256-lru: 8 sets x 2 ways x 16 B, write through, no write allocate, LRU.
inline CacheConfig twoWay256Lru()
{
    return {8, 16, 2, WritePolicy::WriteThrough, AllocationPolicy::NoWriteAllocate,
            ReplacementPolicy::LRU};
}

struct LabRun
{
    std::unique_ptr<RVSSProcessor>  vm;
    std::unique_ptr<CacheLiveObserver> observer; ///< watching L1, if asked for
    std::vector<CacheAccessRecord>  records;     ///< every L1 access, in order

    Cache *l1() const { return vm->memory_controller_.getL1Cache(); }
};

/// Assembles `program`, sets L1 to `l1`, and runs to the end. With `observe`,
/// a CacheLiveObserver watches L1 as the Cache tab's does, and every record
/// it is handed is also kept in full in `records`.
/// `resyncAtEnd` false leaves the observer's shadow exactly as the accesses
/// left it, so a test can check the per-access copying itself.
inline LabRun runOnRvss(const std::string &program, const CacheConfig &l1, bool observe = true,
                        bool resyncAtEnd = true)
{
    LabRun run;
    setupVmStateDirectory();
    run.vm = std::make_unique<RVSSProcessor>();
    run.l1()->reconfigure(l1);
    if (observe)
    {
        run.observer = std::make_unique<CacheLiveObserver>();
        run.observer->attach(run.l1());
        Cache *cache = run.l1();
        auto  *into  = &run.records;
        // Next to the observer's own connections, so this sees exactly the
        // accesses it does, in the same order.
        QObject::connect(cache, &Cache::cacheHitSignal, run.observer.get(),
                         [cache, into] { into->push_back(cache->lastAccess()); },
                         Qt::DirectConnection);
        QObject::connect(cache, &Cache::cacheMissSignal, run.observer.get(),
                         [cache, into] { into->push_back(cache->lastAccess()); },
                         Qt::DirectConnection);
    }
    run.vm->LoadProgram(assemble(examplePath(program)));
    run.vm->DebugRun();
    if (run.observer && resyncAtEnd)
        run.observer->resync(); // the run is over: a full copy is safe
    return run;
}
} // namespace Atlas::test_support::cache_lab

#endif // ATLAS_TEST_CACHE_LAB_TEST_SUPPORT_H
