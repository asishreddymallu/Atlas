/**
 * @file profiler_metrics_test_support.h
 * @brief Runs a program headless on one model and feeds the new metrics'
 *        RetirementStream from the trace after every step, beside a real
 *        Profiler connected to the processor the way ProcessorManager
 *        connects it. No window.
 */
#ifndef ATLAS_TEST_PROFILER_METRICS_TEST_SUPPORT_H
#define ATLAS_TEST_PROFILER_METRICS_TEST_SUPPORT_H

#include "assembler/assembler.h"
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "profiler/profiler.h"
#include "profiler_metrics/instruction_mix.h"
#include "profiler_metrics/retirement_stream.h"
#include "utils/utils.h"

#include <QFile>
#include <QObject>
#include <QTemporaryDir>

#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace Atlas::test_support::metrics
{
inline const std::vector<std::pair<const char *, ProcessorType>> &allModels()
{
    static const std::vector<std::pair<const char *, ProcessorType>> models = {
        {"rvss", ProcessorType::RVSS},
        {"nh_nf", ProcessorType::RV5Stage_NH_NF},
        {"nh_f", ProcessorType::RV5Stage_NH_F},
        {"h_nf", ProcessorType::RV5Stage_H_NF},
        {"h_f", ProcessorType::RV5Stage_H_F}};
    return models;
}

inline std::unique_ptr<ProcessorBase> makeProcessor(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RV5Stage_NH_NF: return std::make_unique<RV5StageProcessorNHNF>();
    case ProcessorType::RV5Stage_NH_F: return std::make_unique<RV5StageProcessorNHF>();
    case ProcessorType::RV5Stage_H_NF: return std::make_unique<RV5StageProcessorHNF>();
    case ProcessorType::RV5Stage_H_F: return std::make_unique<RV5StageProcessorHF>();
    default: return std::make_unique<RVSSProcessor>();
    }
}

struct MetricsRun
{
    bool                           halted{false};
    std::unique_ptr<ProcessorBase> processor;
    std::unique_ptr<Profiler>      profiler;
    AssembledProgram               program;
    MetricsProgram                 info;
    RetirementStream               stream;

    InstructionMix mix() const { return computeInstructionMix(stream.retiredByPc(), info); }
    /// The existing Profiler table's numbers: per-line counts under the line's type.
    std::array<uint64_t, instruction_set::kInstructionTypeCount> existingTypeTable() const
    {
        std::array<uint64_t, instruction_set::kInstructionTypeCount> out{};
        const auto types = profiler->getLineInstructionTypes();
        for (const auto &[line, count] : profiler->getLineExecutionCounts())
        {
            const auto it = types.find(line);
            if (it != types.end() && toIndex(it->second) < out.size())
                out[toIndex(it->second)] += static_cast<uint64_t>(count);
        }
        return out;
    }
};

/// Assembles `path` (an absolute or test-relative file) and steps it to the
/// end (or `cap` steps) on `type`, feeding the stream after every step.
inline MetricsRun runFile(const std::string &path, ProcessorType type, unsigned cap = 2000000)
{
    setupVmStateDirectory();
    MetricsRun run;
    run.program   = assemble(path);
    run.info      = MetricsProgram::from(run.program);
    run.processor = makeProcessor(type);
    run.profiler  = std::make_unique<Profiler>();
    run.profiler->setInstructionToLineMapping(run.program);
    run.profiler->setLineNumberToInstructionTypeMapping(run.program);
    QObject::connect(run.processor.get(), &ProcessorBase::processorClockedSignal, run.profiler.get(),
                     &Profiler::processorClockedSlot, Qt::DirectConnection);
    run.processor->LoadProgram(run.program);
    run.processor->SetBreakpoints({});
    ProcessorBase &p = *run.processor;
    for (unsigned i = 0; i < cap; ++i)
    {
        if (p.NextStepWaitsForInput())
            break;
        const bool more = p.StepAndNotify();
        TraceCycle record;
        if (p.Trace().last(record))
            run.stream.feed(record);
        if (!more)
        {
            run.halted = true;
            break;
        }
    }
    return run;
}

/// Same, from source text.
inline MetricsRun runSource(const std::string &source, ProcessorType type, unsigned cap = 2000000)
{
    static QTemporaryDir dir;
    static int           serial = 0;
    const std::string path = dir.path().toStdString() + "/m" + std::to_string(serial++) + ".s";
    {
        std::ofstream out(path);
        out << source;
    }
    return runFile(path, type, cap);
}

inline uint64_t countOf(const InstructionMix &mix, const std::string &mnemonic)
{
    for (const MnemonicCount &m : mix.byMnemonic)
        if (m.mnemonic == mnemonic)
            return m.count;
    return 0;
}

inline uint64_t typeCount(const InstructionMix &mix, instruction_set::InstructionType t)
{
    return mix.byType[toIndex(t)];
}
} // namespace Atlas::test_support::metrics

#endif // ATLAS_TEST_PROFILER_METRICS_TEST_SUPPORT_H
