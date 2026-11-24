/**
 * @file profiler_metrics_oracle_test.cpp
 * @brief The Profiler metrics against independent values: the mini ISS and
 *        the textbook cache model in verification/, through the generated
 *        table test/profiler_metrics_cases.inc
 *        (verification/metrics/gen_metrics_cases.py; verification/tables/
 *        check_tables.py proves the table is what the generator prints).
 */
#include "profiler_metrics_test_support.h"

#include "profiler_metrics/hot_loops.h"

#include <gtest/gtest.h>

#include <sstream>

using namespace Atlas;
using namespace Atlas::test_support::metrics;

namespace
{
struct MetricsMixCase
{
    const char *program;
    uint64_t    retired, branches, taken, loads, stores;
    const char *mnemonics; ///< "mnemonic=count ..."
    const char *loops;     ///< "targetPc:branchPc:iterations:dynamic ..."
};
struct MetricsMissCase
{
    const char *program, *config;
    size_t      sets, ways, lineBytes;
    bool        fifo, writeBack, writeAllocate;
    uint64_t    misses, compulsory, capacity, conflict, blocks;
};
#include "profiler_metrics_cases.inc"

std::map<std::string, uint64_t> parseMnemonics(const std::string &text)
{
    std::map<std::string, uint64_t> out;
    std::istringstream in(text);
    std::string        item;
    while (in >> item)
    {
        const auto eq = item.find('=');
        out[item.substr(0, eq)] = std::stoull(item.substr(eq + 1));
    }
    return out;
}

std::string path(const char *program)
{
    return std::string("../examples/") + program;
}
} // namespace

TEST(ProfilerMetricsOracle, TheMixMatchesTheMiniIssOnEveryModel)
{
    for (const MetricsMixCase &c : kMixCases)
        for (const auto &[name, type] : allModels())
        {
            const MetricsRun     run = runFile(path(c.program), type);
            const InstructionMix mix = run.mix();
            const std::string    where = std::string(c.program) + " on " + name;
            ASSERT_TRUE(run.halted) << where;
            EXPECT_EQ(mix.retired, c.retired) << where;
            EXPECT_EQ(mix.loads, c.loads) << where;
            EXPECT_EQ(mix.stores, c.stores) << where;
            std::map<std::string, uint64_t> got;
            for (const MnemonicCount &m : mix.byMnemonic)
                got[m.mnemonic] = m.count;
            EXPECT_EQ(got, parseMnemonics(c.mnemonics)) << where;
        }
}

TEST(ProfilerMetricsOracle, BranchesAndTakenMatchTheMiniIssOnEveryModel)
{
    for (const MetricsMixCase &c : kMixCases)
        for (const auto &[name, type] : allModels())
        {
            const MetricsRun  run   = runFile(path(c.program), type);
            const std::string where = std::string(c.program) + " on " + name;
            EXPECT_EQ(run.stream.branches(), c.branches) << where;
            EXPECT_EQ(run.stream.branchesTaken(), c.taken) << where;
        }
}

TEST(ProfilerMetricsOracle, HottestLoopsMatchTheMiniIss)
{
    for (const MetricsMixCase &c : kMixCases)
        for (const auto &[name, type] : allModels())
        {
            const MetricsRun run = runFile(path(c.program), type);
            std::ostringstream got;
            for (const HotLoop &loop : findHotLoops(run.info, run.stream.retiredByPc(), run.stream.retired()))
                got << (got.tellp() > 0 ? " " : "") << loop.targetPc << ":" << loop.branchPc << ":"
                    << loop.iterations << ":" << loop.dynamic;
            EXPECT_EQ(got.str(), c.loops) << c.program << " on " << name;
        }
}
