/**
 * @file cache_lab_expected_test.cpp
 * @brief The lab's expected results, measured in the Cache tab.
 *
 * Each row is a lab preset (src/cache_lab/lab_presets.cpp) and a program
 * from examples/cache_lab, run in the real window on RVSS with the window's
 * own Run, exactly as the Cache tab tests do (cache_window_harness.h). The
 * L1 counts are the same on all five processors (E8).
 *
 * The expected values came from the lab manual, which is not in the repo.
 * Some had been measured outside the repo and some were only predicted; the
 * predicted ones are marked. -1 means the manual gave no value for that
 * column. Only rows whose measurement matched are pinned here; a row that
 * differed would be left out and reported with both numbers. Measured on
 * 2026-11-24: all 28 rows matched, the predicted ones included (dm128
 * temporal_repeat 192/64/56; dm256-32B stride_1 896/128/120, and the hit
 * counts 384, 128 and 0 for stride_2, 4 and 8).
 */
#include "cache_window_harness.h"

#include "cache_lab/lab_presets.h"

#include <QDir>

#include <gtest/gtest.h>

#include <iostream>
#include <ostream>
#include <string>

using namespace Atlas;
using namespace Atlas::test_support;

namespace
{
struct ExpectedRow
{
    const char *preset;
    const char *program;
    long long   hits, misses, evictions, writeBacks; ///< -1: not given
    const char *script;                              ///< custom Lua policy, or nullptr
    bool        predicted;                           ///< only predicted in the manual
};

void PrintTo(const ExpectedRow &r, std::ostream *os)
{
    *os << r.preset << "/" << r.program << (r.script ? std::string("/") + r.script : "");
}

constexpr long long N = -1;

const ExpectedRow kRows[] = {
    {"dm256", "stride_1", 768, 256, 240, N, nullptr, false},
    {"dm256", "stride_2", 256, 256, 240, N, nullptr, false},
    {"dm256", "stride_4", 0, 256, 240, N, nullptr, false},
    {"dm256", "stride_8", 0, 128, 120, N, nullptr, false},
    {"dm256", "stride_16", 0, 64, 60, N, nullptr, false},
    {"dm256", "stride_32", 0, 32, 30, N, nullptr, false},
    {"dm256", "temporal_repeat", 240, 16, 0, N, nullptr, false},
    {"dm256", "matmul_ijk", 853, 363, 155, N, nullptr, false},
    {"dm256", "matmul_jik", 817, 399, 191, N, nullptr, false},
    {"dm1024", "stride_1", 768, 256, 192, N, nullptr, false},
    {"dm1024", "stride_16", 0, 64, 48, N, nullptr, false},
    {"dm1024", "matmul_ijk", 992, 224, 0, N, nullptr, false},
    {"dm1024", "matmul_jik", 992, 224, 0, N, nullptr, false},
    {"dm128", "temporal_repeat", 192, 64, 56, N, nullptr, true},
    {"2w128-fifo", "temporal_repeat", 128, 128, 112, N, nullptr, false},
    {"dm256-32B", "stride_1", 896, 128, 120, N, nullptr, true},
    {"dm256-32B", "stride_2", 384, N, N, N, nullptr, true},
    {"dm256-32B", "stride_4", 128, N, N, N, nullptr, true},
    {"dm256-32B", "stride_8", 0, N, N, N, nullptr, true},
    {"2w256-lru", "matmul_ijk", 827, 389, 181, N, nullptr, false},
    {"2w256-lru", "matmul_jik", 756, 460, 252, N, nullptr, false},
    {"2w256-fifo", "matmul_ijk", 788, 428, 220, N, nullptr, false},
    {"2w256-fifo", "matmul_jik", 805, 411, 203, N, nullptr, false},
    {"4w256-lru", "matmul_ijk", 691, 525, 317, N, nullptr, false},
    {"2w256-lru-wb", "matmul_ijk", 886, 330, 314, 94, nullptr, false},
    {"2w256-fifo-wb", "matmul_ijk", 888, 328, 312, 94, nullptr, false},
    {"2w256-lru", "matmul_ijk", 827, 389, 181, N, "lru.lua", false},
    {"2w256-lru", "matmul_ijk", 788, 428, 220, N, "fifo.lua", false},
};

class CacheLabExpected : public ::testing::TestWithParam<ExpectedRow>
{
};

std::string number(long long v)
{
    return std::to_string(v);
}
} // namespace

TEST_P(CacheLabExpected, MeasuredInTheCacheTab)
{
    const ExpectedRow &row = GetParam();
    CacheWindow        w;
    ASSERT_TRUE(w.ready());
    w.use(ProcessorType::RVSS);
    ASSERT_TRUE(w.cacheTab->applyLabPreset(QString::fromUtf8(row.preset)));
    if (row.script)
    {
        // As the Custom choice's file dialog does.
        const QString path =
            QDir(QStringLiteral("../examples/custom-replacement-scripts")).absoluteFilePath(row.script);
        emit w.level("L1Config")->customPolicyScriptSelectedSignal(path.toStdString());
        QApplication::processEvents();
        ASSERT_EQ(w.manager->getMemoryController()->getL1Cache()->getConfig().replacementPolicy,
                  ReplacementPolicy::Custom);
    }
    ASSERT_TRUE(w.runSource(readCacheLabExample(QString::fromUtf8(row.program))));
    EXPECT_TRUE(w.modals.isEmpty()) << w.modals.join(" | ").toStdString();

    const std::string hits   = w.field("L1Config", "hitsLineEdit").toStdString();
    const std::string misses = w.field("L1Config", "missesLineEdit").toStdString();
    const std::string evicts = w.field("L1Config", "evictionsLineEdit").toStdString();
    const std::string wbs    = w.field("L1Config", "writeBackLineEdit").toStdString();
    std::cout << "[measured] " << row.preset << " " << row.program
              << (row.script ? std::string(" ") + row.script : "") << ": " << hits << "/" << misses
              << "/" << evicts << " wb " << wbs << " rate "
              << w.field("L1Config", "hitrateLineEdit").toStdString() << std::endl;

    if (row.hits >= 0)
        EXPECT_EQ(hits, number(row.hits));
    if (row.misses >= 0)
        EXPECT_EQ(misses, number(row.misses));
    if (row.evictions >= 0)
        EXPECT_EQ(evicts, number(row.evictions));
    if (row.writeBacks >= 0)
        EXPECT_EQ(wbs, number(row.writeBacks));
}

INSTANTIATE_TEST_SUITE_P(LabManual, CacheLabExpected, ::testing::ValuesIn(kRows),
                         [](const ::testing::TestParamInfo<ExpectedRow> &info)
                         {
                             std::string label = std::string(info.param.preset) + "_" +
                                                 info.param.program +
                                                 (info.param.script ? std::string("_") + info.param.script
                                                                    : "");
                             for (char &c : label)
                                 if (c == '-' || c == '.')
                                     c = '_';
                             return label;
                         });
