/**
 * @file cache_miss_causes_test.cpp
 * @brief Metric 6, misses by cause: the classifier, hand-computed programs,
 *        the 28 pinned lab rows in the real window, compulsory = distinct
 *        blocks, no conflict misses when fully associative, the independent
 *        Python 3C split, and the live view's line.
 */
#include "cache_lab_test_support.h"
#include "cache_window_harness.h"

#include "cache_lab/miss_classifier.h"
#include "ui/cache_tab/cache_live_view.h"

#include <QDir>

#include <gtest/gtest.h>

#include <set>

using namespace Atlas;
using namespace Atlas::test_support;
using namespace Atlas::test_support::cache_lab;

namespace
{
struct MetricsMixCase
{
    const char *program;
    uint64_t    retired, branches, taken, loads, stores;
    const char *mnemonics, *loops;
};
struct MetricsMissCase
{
    const char *program, *config;
    size_t      sets, ways, lineBytes;
    bool        fifo, writeBack, writeAllocate;
    uint64_t    misses, compulsory, capacity, conflict, blocks;
};
#include "profiler_metrics_cases.inc"

CacheConfig configOf(size_t sets, size_t ways, size_t line, bool fifo, bool wb, bool wa)
{
    CacheConfig c{};
    c.lineCount         = sets; // the set count (known_issues 131)
    c.wayCount          = ways;
    c.lineSizeInBytes   = line;
    c.replacementPolicy = fifo ? ReplacementPolicy::FIFO : ReplacementPolicy::LRU;
    c.writePolicy       = wb ? WritePolicy::WriteBack : WritePolicy::WriteThrough;
    c.allocationPolicy  = wa ? AllocationPolicy::WriteAllocate : AllocationPolicy::NoWriteAllocate;
    return c;
}

/// runOnRvss with L2 set as the verification probe sets it (32 sets x 4
/// ways x 64 B, LRU): the default L2's line is shorter than some lab L1
/// lines, which the cache does not allow. L1's counts do not depend on L2.
MissCauses causesOnRvss(const std::string &program, const CacheConfig &l1, uint64_t *misses)
{
    setupVmStateDirectory();
    auto vm = std::make_unique<RVSSProcessor>();
    vm->memory_controller_.getL2Cache()->reconfigure({32, 64, 4, WritePolicy::WriteThrough,
                                                      AllocationPolicy::NoWriteAllocate,
                                                      ReplacementPolicy::LRU});
    Cache *cache = vm->memory_controller_.getL1Cache();
    cache->reconfigure(l1);
    CacheLiveObserver observer;
    observer.attach(cache);
    vm->LoadProgram(assemble(examplePath(program)));
    vm->DebugRun();
    *misses = cache->getMissCount();
    return observer.state().causes;
}

std::string programName(const char *path) // "cache_lab/stride_1.s" -> "stride_1"
{
    std::string p = path;
    p             = p.substr(p.find('/') + 1);
    return p.substr(0, p.size() - 2);
}
} // namespace

// ---------------------------------------------------------------- classifier

TEST(CacheMissCauses, TheClassifierFollowsTheDefinitions)
{
    // Two sets of one line, 16-byte lines: blocks 0 and 2 share set 0.
    MissClassifier c;
    c.configure(2, 1, 16, false);
    c.access(0, false, false);  // block 0: compulsory
    c.access(32, false, false); // block 2: compulsory (evicts 0 in the real cache)
    c.access(0, false, false);  // block 0 again: a 2-line LRU cache holds both -> conflict
    c.access(16, false, false); // block 1: compulsory
    c.access(48, false, false); // block 3: compulsory
    c.access(32, false, false); // block 2: the 2-line LRU cache dropped it -> capacity
    c.access(32, false, true);  // a hit is not classified
    const MissCauses &m = c.causes();
    EXPECT_EQ(m.misses, 6u);
    EXPECT_EQ(m.compulsory, 4u);
    EXPECT_EQ(m.conflict, 1u);
    EXPECT_EQ(m.capacity, 1u);
    EXPECT_EQ(m.compulsory + m.capacity + m.conflict, m.misses);
    EXPECT_EQ(m.distinctBlocks, 4u);
    EXPECT_EQ(m.accesses, 7u);
    c.reset();
    EXPECT_EQ(c.causes().misses, 0u);
}

TEST(CacheMissCauses, AWriteMissWithoutWriteAllocateFillsNothing)
{
    MissClassifier c;
    c.configure(1, 1, 16, false);
    c.access(0, true, false);  // write miss, not allocated: compulsory
    c.access(0, false, false); // read: still a miss; the model did not fill it either -> capacity
    EXPECT_EQ(c.causes().compulsory, 1u);
    EXPECT_EQ(c.causes().capacity, 1u);
    EXPECT_EQ(c.causes().conflict, 0u);
}

// ---------------------------------------------------------------- programs

TEST(CacheMissCauses, Stride1AndTemporalRepeatByHand)
{
    // stride_1 on dm256 reads 4 KB once: 256 blocks of 16 B, each missed once
    // (compulsory), never again. temporal_repeat sweeps the same 256 B four
    // times: its 16 blocks fit exactly, so 16 compulsory misses and no others.
    const LabRun a = runOnRvss("stride_1", dm256());
    MissCauses   s = a.observer->state().causes;
    EXPECT_EQ(s.misses, 256u);
    EXPECT_EQ(s.compulsory, 256u);
    EXPECT_EQ(s.capacity, 0u);
    EXPECT_EQ(s.conflict, 0u);
    EXPECT_EQ(a.l1()->getMissCount(), 256u);

    const LabRun b = runOnRvss("temporal_repeat", dm256());
    s              = b.observer->state().causes;
    EXPECT_EQ(s.misses, 16u);
    EXPECT_EQ(s.compulsory, 16u);
    EXPECT_EQ(s.capacity + s.conflict, 0u);
}

TEST(CacheMissCauses, CompulsoryEqualsTheDistinctBlocksTouched)
{
    for (const char *program : {"stride_1", "stride_4", "temporal_repeat", "matmul_ijk", "matmul_jik"})
        for (const CacheConfig &cfg : {dm256(), twoWay256Lru()})
        {
            const LabRun       run = runOnRvss(program, cfg);
            std::set<uint64_t> blocks;
            for (const CacheAccessRecord &r : run.records)
                blocks.insert(r.address / cfg.lineSizeInBytes);
            const MissCauses s = run.observer->state().causes;
            EXPECT_EQ(s.compulsory, blocks.size()) << program;
            EXPECT_EQ(s.compulsory + s.capacity + s.conflict, run.l1()->getMissCount()) << program;
        }
}

TEST(CacheMissCauses, AFullyAssociativeLruCacheHasNoConflictMisses)
{
    // One set of 16 ways (256 B, LRU) is the classifier's own model.
    for (const char *program : {"stride_1", "stride_16", "temporal_repeat", "matmul_ijk", "matmul_jik"})
    {
        const LabRun     run = runOnRvss(program, configOf(1, 16, 16, false, false, false));
        const MissCauses s   = run.observer->state().causes;
        EXPECT_EQ(s.conflict, 0u) << program;
        EXPECT_EQ(s.compulsory + s.capacity, run.l1()->getMissCount()) << program;
    }
}

TEST(CacheMissCauses, TheSplitMatchesThePythonModel)
{
    // verification/metrics/gen_metrics_cases.py: cachemodel.py with a fully
    // associative LRU shadow, 9 programs x 7 configurations.
    int checked = 0;
    for (const MetricsMissCase &c : kMissCases)
    {
        uint64_t         misses = 0;
        const MissCauses s      = causesOnRvss(programName(c.program),
                                               configOf(c.sets, c.ways, c.lineBytes, c.fifo, c.writeBack,
                                                        c.writeAllocate),
                                               &misses);
        const std::string where = std::string(c.program) + " " + c.config;
        EXPECT_EQ(misses, c.misses) << where;
        EXPECT_EQ(s.misses, c.misses) << where;
        EXPECT_EQ(s.compulsory, c.compulsory) << where;
        EXPECT_EQ(s.capacity, c.capacity) << where;
        EXPECT_EQ(s.conflict, c.conflict) << where;
        ++checked;
    }
    EXPECT_EQ(checked, 63);
}

// ---------------------------------------------------------------- the window

namespace
{
struct PinnedRow
{
    const char *preset, *program, *script;
};
// The 28 rows test/cache_lab_expected_test.cpp pins.
const PinnedRow kPinned[] = {
    {"dm256", "stride_1", nullptr},         {"dm256", "stride_2", nullptr},
    {"dm256", "stride_4", nullptr},         {"dm256", "stride_8", nullptr},
    {"dm256", "stride_16", nullptr},        {"dm256", "stride_32", nullptr},
    {"dm256", "temporal_repeat", nullptr},  {"dm256", "matmul_ijk", nullptr},
    {"dm256", "matmul_jik", nullptr},       {"dm1024", "stride_1", nullptr},
    {"dm1024", "stride_16", nullptr},       {"dm1024", "matmul_ijk", nullptr},
    {"dm1024", "matmul_jik", nullptr},      {"dm128", "temporal_repeat", nullptr},
    {"2w128-fifo", "temporal_repeat", nullptr}, {"dm256-32B", "stride_1", nullptr},
    {"dm256-32B", "stride_2", nullptr},     {"dm256-32B", "stride_4", nullptr},
    {"dm256-32B", "stride_8", nullptr},     {"2w256-lru", "matmul_ijk", nullptr},
    {"2w256-lru", "matmul_jik", nullptr},   {"2w256-fifo", "matmul_ijk", nullptr},
    {"2w256-fifo", "matmul_jik", nullptr},  {"4w256-lru", "matmul_ijk", nullptr},
    {"2w256-lru-wb", "matmul_ijk", nullptr}, {"2w256-fifo-wb", "matmul_ijk", nullptr},
    {"2w256-lru", "matmul_ijk", "lru.lua"}, {"2w256-lru", "matmul_ijk", "fifo.lua"},
};
} // namespace

TEST(CacheMissCauses, OnEveryPinnedLabRowTheCausesAddUpToTheMisses)
{
    static_assert(std::size(kPinned) == 28);
    for (const PinnedRow &row : kPinned)
    {
        CacheWindow w;
        ASSERT_TRUE(w.ready());
        w.use(ProcessorType::RVSS);
        ASSERT_TRUE(w.cacheTab->applyLabPreset(QString::fromUtf8(row.preset)));
        if (row.script)
        {
            const QString path =
                QDir(QStringLiteral("../examples/custom-replacement-scripts")).absoluteFilePath(row.script);
            emit w.level("L1Config")->customPolicyScriptSelectedSignal(path.toStdString());
            QApplication::processEvents();
        }
        ASSERT_TRUE(w.runSource(readCacheLabExample(QString::fromUtf8(row.program))));
        Cache *l1 = w.manager->getMemoryController()->getL1Cache();
        const MissCauses s = w.cacheTab->liveObserver(CacheTab::L1)->state().causes;
        const std::string where = std::string(row.preset) + " " + row.program + (row.script ? row.script : "");
        EXPECT_EQ(s.misses, l1->getMissCount()) << where;
        EXPECT_EQ(s.compulsory + s.capacity + s.conflict, l1->getMissCount()) << where;
        EXPECT_EQ(w.field("L1Config", "missesLineEdit").toStdString(), std::to_string(l1->getMissCount())) << where;
    }
}

TEST(CacheMissCauses, TheLiveViewShowsTheSplitAndTheWindowKeepsItsSize)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    const QSize before = w.window.minimumSizeHint();
    w.use(ProcessorType::RVSS);
    ASSERT_TRUE(w.cacheTab->applyLabPreset(QStringLiteral("dm256")));
    ASSERT_TRUE(w.runSource(readCacheLabExample(QStringLiteral("stride_1"))));
    w.cacheTab->resyncLiveView();
    w.cacheTab->refreshLiveView();
    EXPECT_EQ(w.cacheTab->liveView(CacheTab::L1)->missCausesText(),
              QStringLiteral("Misses by cause [Modelled]: compulsory 256 · capacity 0 · conflict 0"));
    EXPECT_EQ(w.window.minimumSizeHint(), before);
}
