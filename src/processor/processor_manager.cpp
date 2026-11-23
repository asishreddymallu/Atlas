/**
 * @file vm_manager.cpp
 * @brief This file contains the implementation of the VMManager class
 * @version 0.1
 * @date 2025-10-23
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "processor/processor_factory.h"
#include <QFile>
#include <QJsonDocument>
#include "processor/rv5s/rv5s_processor_h_f.h"
#include "processor/rv5s/rv5s_processor_h_nf.h"
#include "processor/rv5s/rv5s_processor_nh_f.h"
#include "processor/rv5s/rv5s_processor_nh_nf.h"
#include "processor/rvss/rvss_processor.h"
#include "processor/processor_manager.h"

#include <QCoreApplication>
#include <QMutexLocker>
#include <algorithm>
#include <QEvent>
#include <QtGlobal>
#include "common/globals.h"
#include "common/assembled_program.h"
#include "assembler/assembler.h"
#include "utils/utils.h"

namespace Atlas
{
ProcessorManager::ProcessorManager(QObject *parent, ProcessorType vmType) : QObject(parent)
{
    // Before the first processor below: its constructor dumps registers and
    // state into vm_state/, which a clean checkout does not have yet.
    setupVmStateDirectory();
    // first we register all the VMs
    // m_profiler = std::make_unique<Profiler>();

    ProcessorFactory::RegisterVM<RVSSProcessor>(ProcessorType::RVSS);
    ProcessorFactory::RegisterVM<RV5StageProcessorNHNF>(ProcessorType::RV5Stage_NH_NF);
    ProcessorFactory::RegisterVM<RV5StageProcessorHNF>(ProcessorType::RV5Stage_H_NF);
    ProcessorFactory::RegisterVM<RV5StageProcessorNHF>(ProcessorType::RV5Stage_NH_F);
    ProcessorFactory::RegisterVM<RV5StageProcessorHF>(ProcessorType::RV5Stage_H_F);
    m_currentProcessorType = vmType;
    m_currentProcessor = ProcessorFactory::createVM(vmType);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorStateChangedSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorPausedAtBreakpointSignal, this,
            &ProcessorManager::processorPausedAtBreakpointSignal, Qt::DirectConnection);
    // here we connect the vm state changed signal to the vm manager signal
    // and this will be further connected to the mainwindow slot to update the ui
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSlot, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSignal, Qt::DirectConnection);
    // As in changeProcessor: program output and input requests (140).
    connect(m_currentProcessor.get(), &ProcessorBase::programOutputSignal, this,
            &ProcessorManager::programOutputSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::inputRequestedSignal, this,
            &ProcessorManager::inputRequestedSignal, Qt::DirectConnection);

    connect(this, &ProcessorManager::processorClockedSignal,&m_profiler,
            &Profiler::processorClockedSlot, Qt::DirectConnection);
}

void ProcessorManager::changeProcessor(ProcessorType vmType)
{
    // The assignment below destroys the current processor. If the worker
    // were inside Run() on it that is a use-after-free, so stop first.
    // The UI disables this action during a run, which is why it has not
    // been seen; it is one line to make it safe regardless.
    if (m_currentProcessor && !m_currentProcessor->StopAndReset())
    {
        qWarning("Atlas: processor did not stop; not switching processor");
        return;
    }
    m_currentProcessorType = vmType;
    m_currentProcessor = ProcessorFactory::createVM(vmType);
    // The new processor has no program, so there is nothing to profile.
    m_profiler.Reset();
    m_currentProcessor->step_delay_ = m_stepDelayMs;
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorStateChangedSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSlot, Qt::DirectConnection);        
    connect(m_currentProcessor.get(), &ProcessorBase::processorPausedAtBreakpointSignal, this,
            &ProcessorManager::processorPausedAtBreakpointSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSignal, Qt::DirectConnection);
    // Program output and input requests, for the window's Output panel
    // (known_issues 140). Forwarded as they come; the window queues them.
    connect(m_currentProcessor.get(), &ProcessorBase::programOutputSignal, this,
            &ProcessorManager::programOutputSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::inputRequestedSignal, this,
            &ProcessorManager::inputRequestedSignal, Qt::DirectConnection);
    // create new connections for the new VM

    // The new processor has run nothing: no stage holds an instruction, so
    // nothing is selected until the next clock. The cursor line is kept.
    {
        QMutexLocker lock(&m_selectionMutex);
        m_lastProgramCounters.clear();
    }
    refreshSelectedStages();
    emit processorStateChangedSignal(); // a new processor: new counters, new n/a rows
}

void ProcessorManager::setSelectedSourceLine(int line)
{
    {
        QMutexLocker lock(&m_selectionMutex);
        m_selectedSourceLine = line;
    }
    refreshSelectedStages();
}

quint8 ProcessorManager::selectedStages() const
{
    QMutexLocker lock(&m_selectionMutex);
    return m_selectedStages;
}

void ProcessorManager::refreshSelectedStages()
{
    quint8 stages = 0;
    {
        QMutexLocker lock(&m_selectionMutex);
        // The inverse of updateEditorHighlight: stage i is selected when
        // the instruction its label shows (programCounters[i], in the order
        // IF, ID, EX, MEM, WB) comes from the cursor's line. RVSS has one
        // "." marker and no stages, so it never selects.
        if (m_currentProcessorType != ProcessorType::RVSS && m_selectedSourceLine > 0)
        {
            const auto &mapping = m_currentProgram.instruction_number_line_number_mapping;
            const size_t count  = std::min<size_t>(m_lastProgramCounters.size(), 5);
            for (size_t i = 0; i < count; ++i)
            {
                const uint64_t pc = m_lastProgramCounters[i];
                if (pc == INVALID_PC)
                    continue;
                const auto it = mapping.find(static_cast<unsigned int>(pc / 4));
                if (it != mapping.end() && static_cast<int>(it->second) == m_selectedSourceLine)
                    stages |= stageBit(static_cast<WireStage>(i));
            }
        }
        m_selectedStages = stages;
    }
    emit selectedStagesSignal(stages);
}

void ProcessorManager::loadProgram(const AssembledProgram &program)
{
    m_currentProgram = program;
    m_currentProcessor->LoadProgram(program);
    // A new program starts a new profile (known_issues 85: Reset had no
    // callers, so counts carried over from the last program).
    m_profiler.Reset();
    m_profiler.setInstructionToLineMapping(program);
    m_profiler.setLineNumberToInstructionTypeMapping(program);
    updateEditorHighlight({0});
    emit processorStateChangedSignal(); // the counters are back to 0
}

void ProcessorManager::loadProgram(const std::string &sourceText)
{
    std::ofstream out(globals::temporary_assembly_file_path);
    out << sourceText;
    out.close();
    m_currentProgram = assemble(globals::temporary_assembly_file_path.string());
    m_currentProcessor->LoadProgram(m_currentProgram);
    DumpDisasssembly(globals::disassembly_file_path, m_currentProgram);
    std::ifstream in(globals::disassembly_file_path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    in.close();
    updateEditorHighlight({0});
    emit updateDisassemblySignal(QString::fromStdString(buffer.str()));
}

void ProcessorManager::requestRun()
{
    // UI thread. Snapshot the epoch NOW and carry it with the posted call,
    // so a stop issued between here and the worker picking the call up
    // cancels this run.
    QMetaObject::invokeMethod(this, "runSlotEpoch", Qt::QueuedConnection,
                              Q_ARG(quint64, m_currentProcessor->CurrentStopEpoch()));
}

void ProcessorManager::requestDebugRun()
{
    QMetaObject::invokeMethod(this, "debugRunSlotEpoch", Qt::QueuedConnection,
                              Q_ARG(quint64, m_currentProcessor->CurrentStopEpoch()));
}

void ProcessorManager::runSlotEpoch(quint64 epoch)
{
    // BeginRun checks the epoch and marks the worker busy in ONE critical
    // section. Without that, a stop could land between the check and
    // Run()'s RunScope, see worker_in_run_ still false, reset, and let the
    // run start on the freshly reset state - measured at 5 of 5 before
    // this existed.
    if (!m_currentProcessor->BeginRun(epoch))
    {
        // Overtaken by a stop: do not run, do not ClearStop. The UI still
        // has to be told, because it disabled the execution actions when
        // it asked for this run and runFinishedSlot is what re-enables
        // them. Returning silently leaves the Processor action disabled
        // with no run in progress - measured as a stuck UI in the
        // run-then-immediate-stop scenario.
        // debugRunSlotEpoch set start_paused_ before getting here. Nothing
        // consumed it, because the clear inside BeginRun never ran, so the
        // NEXT plain Run would silently start paused - a window that looks
        // exactly like a hang.
        m_currentProcessor->start_paused_ = false;
        emit runFinishedSignal();
        return;
    }
    run();
    m_currentProcessor->MarkRunExited(); // idempotent with Run()'s RunScope
    emit runFinishedSignal();
}

void ProcessorManager::debugRunSlotEpoch(quint64 epoch)
{
    m_currentProcessor->start_paused_ = true;
    runSlotEpoch(epoch);
}

void ProcessorManager::runSlot()
{
    // Kept for callers with no epoch of their own (the out-of-tree
    // harnesses). Uses the current epoch, so it always runs.
    runSlotEpoch(m_currentProcessor->CurrentStopEpoch());
}

void ProcessorManager::debugRunSlot()
{
    m_currentProcessor->start_paused_ = true;
    runSlot();
}

void ProcessorManager::processorClockedSlot(const ProcessorState &processorState)
{
    //first we get all the necesary info from the prcossor and then update the related
    //gui widgets
    updateEditorHighlight(processorState.programCounters);

}


void ProcessorManager::updateEditorHighlight(const std::vector<uint64_t>& programCounters)
{
    // The circuit view's stage selection reads the same PCs.
    {
        QMutexLocker lock(&m_selectionMutex);
        m_lastProgramCounters = programCounters;
    }
    refreshSelectedStages();

    // Nothing has been clocked yet, so there is no stage to point at.
    //
    // RVSSProcessor::Run() emits processorClockedSignal(processor_state_)
    // once more after its loop, and on a Debug Run that emit can be reached
    // before any step has run - setProcessorState() is what fills
    // programCounters, and it has not been called. The RVSS branch below
    // then read programCounters[0] on a default-constructed vector, whose
    // data() is null: an abort under libstdc++ assertions, which this
    // toolchain has on by default, and a segmentation fault without them.
    // It is undefined behaviour either way, not a stale read.
    if (programCounters.empty())
    {
        return;
    }

    std::vector<std::pair<int,std::string>> editorLines;
    std::vector<std::pair<int,std::string>> disassemblyLines;
    const auto addHighlightIfMapped = [&](const std::map<unsigned int, unsigned int> &mapping,
                                          unsigned int instructionNumber,
                                          const std::string &stageLabel,
                                          std::vector<std::pair<int, std::string>> &lines)
    {
        const auto it = mapping.find(instructionNumber);
        if (it == mapping.end())
        {
            return false;
        }

        lines.emplace_back(static_cast<int>(it->second), stageLabel);
        return true;
    };

    static const std::array<std::string, 5> pcToStageLable = {"IF","ID", "EX", "MEM", "WB"};

    if(m_currentProcessorType == ProcessorType::RVSS)
    {
        auto programCounter = programCounters[0];
        const auto instructionNumber = static_cast<unsigned int>(programCounter / 4);
        addHighlightIfMapped(m_currentProgram.instruction_number_line_number_mapping,
                             instructionNumber,
                             ".",
                             editorLines);
        addHighlightIfMapped(m_currentProgram.instruction_number_disassembly_mapping,
                             instructionNumber,
                             ".",
                             disassemblyLines);
    }
    else
    {

        for(size_t i = 0; i < programCounters.size(); ++i)
        {
            auto stagePC = programCounters[i];
            if(stagePC == INVALID_PC) continue; // skip stages that are not active

            const auto instructionNumber = static_cast<unsigned int>(stagePC / 4);
            const auto stageLabel = pcToStageLable[i];

            addHighlightIfMapped(m_currentProgram.instruction_number_line_number_mapping,
                                 instructionNumber,
                                 stageLabel,
                                 editorLines);
            addHighlightIfMapped(m_currentProgram.instruction_number_disassembly_mapping,
                                 instructionNumber,
                                 stageLabel,
                                 disassemblyLines);
            
        }
        
    }
    emit updateEditorHighlightSignal(editorLines, disassemblyLines);
}

int ProcessorManager::resolveCurrentSourceLine() const
{
    try
    {
        const uint64_t programCounter = m_currentProcessor->program_counter_;
        return static_cast<int>(
            m_currentProgram.instruction_number_line_number_mapping.at(programCounter / 4));
    }
    catch (const std::out_of_range &)
    {
        return -1;
    }
}

void ProcessorManager::run()
{
    try
    {
        m_currentProcessor->Run();
    }
    catch (const std::exception &e)
    {
        emit runErrorSignal(QString::fromStdString(e.what()), resolveCurrentSourceLine());
    }
}

void ProcessorManager::step()
{
    // At the end there is nothing to step. Stepping used to reach the reset
    // below; now the end is a state the program stays in until Undo walks
    // back from it or a reload, Stop or switch replaces it.
    if (m_currentProcessor->ProgramEnded())
        return;
    try
    {
        if (!m_currentProcessor->StepAndNotify())
        {
            // The step that ended the program. Let a Debug Run's worker
            // leave Run(), so the window goes idle, and keep everything the
            // program produced. This was RequestStop(), i.e. StopAndReset():
            // it wiped the final state on the step that made it
            // (known_issues 142).
            m_currentProcessor->StopKeepingState();
        }
    }
    catch (const std::exception &e)
    {
        emit runErrorSignal(QString::fromStdString(e.what()), resolveCurrentSourceLine());
    }
}

void ProcessorManager::debugRun()
{
    // Debug mode starts in paused/manual mode; user advances with Step.
    m_currentProcessor->ClearStop();
    m_currentProcessor->RequestPause();
}

bool ProcessorManager::nextStepWaitsForInput()
{
    return m_currentProcessor->NextStepWaitsForInput();
}

bool ProcessorManager::programEnded() const
{
    return m_currentProcessor->ProgramEnded();
}

bool ProcessorManager::stop()
{
    return m_currentProcessor->StopAndReset();
}
void ProcessorManager::pause()
{
    m_currentProcessor->RequestPause();
}
void ProcessorManager::resume()
{
    m_currentProcessor->RequestResume();
}
void ProcessorManager::undo()
{
    m_currentProcessor->Undo();
}
void ProcessorManager::redo()
{
    // Redo restores a saved cycle rather than running it, so the state it
    // emits has no fresh retiredPC; the profiler replays what it undid.
    m_profiler.setReplayingRedo(true);
    m_currentProcessor->Redo();
    m_profiler.setReplayingRedo(false);
}

void ProcessorManager::pushInput(const std::string &input)
{
    m_currentProcessor->PushInput(input);
}
void ProcessorManager::setStepDelay(unsigned int delay)
{
    m_stepDelayMs = delay;
    m_currentProcessor->step_delay_ = delay;
}

void ProcessorManager::setBreakpoints(const std::vector<uint64_t> &breakpoints)
{
    m_currentProcessor->SetBreakpoints(breakpoints);
}

RegisterFile *ProcessorManager::getRegisterFile() const
{
    return &m_currentProcessor->registers_;
}

MemoryController *ProcessorManager::getMemoryController() const
{
    return &m_currentProcessor->memory_controller_;
}

Atlas::CircuitScene *ProcessorManager::getCircuitScene() const
{
    return m_currentProcessor->circuit_scene_.get();
}
bool ProcessorManager::reset()
{
    // Called from the UI thread (MainWindow::tryParseAndLoadProgram) while
    // the worker may still be inside Run(). StopAndReset stops the worker,
    // waits for it to leave Run(), and only then clears - known_issues 62.
    // The stop epoch (bumped inside StopAndReset) is what cancels a run
    // that was requested before this stop, including one already taken
    // off the queue by the worker.
    // The result is returned, not swallowed: on timeout nothing was
    // cleared, so a caller that went on to loadProgram() would be loading
    // onto a processor that is still running - the original race.
    if (!m_currentProcessor->StopAndReset())
    {
        qWarning("Atlas: the processor did not stop within the timeout; "
                 "nothing was reset and nothing should be loaded");
        return false;
    }
    return true;
}


ProcessorType ProcessorManager::getProcessorType()
{
    return m_currentProcessorType;
}

const Profiler* ProcessorManager::getProfiler() const
{
    return &m_profiler;
}

uint64_t ProcessorManager::getProgramCounter() const
{
    return m_currentProcessor->program_counter_;
}

float ProcessorManager::getCPI() const
{
    return m_currentProcessor->cpi_;
}

float ProcessorManager::getIPC() const
{
    return m_currentProcessor->ipc_;
}

unsigned int ProcessorManager::getBranchMispredictions() const
{
    return m_currentProcessor->branch_mispredictions_;
}

unsigned int ProcessorManager::getStallCycles() const
{
    return m_currentProcessor->stall_cycles_;
}

unsigned int ProcessorManager::getCycles() const
{
    return m_currentProcessor->cycle_s_;
}

ProcessorMetrics ProcessorManager::metrics() const
{
    const ProcessorBase &p = *m_currentProcessor;
    ProcessorMetrics     m;
    m.cycles  = p.cycle_s_;
    m.retired = p.instructions_retired_;
    m.cpi     = p.cpi_;
    m.ipc     = p.ipc_;

    const bool singleCycle = m_currentProcessorType == ProcessorType::RVSS;
    const bool hazardUnit  = m_currentProcessorType == ProcessorType::RV5Stage_H_NF ||
                            m_currentProcessorType == ProcessorType::RV5Stage_H_F;
    const TraceTotals split = p.TraceTotalsNow();
    if (!singleCycle)
    {
        m.flushedBranch = split.flushedBranch;
        m.flushedJump   = split.flushedJump;
        m.flushedTrap   = split.flushedTrap;
        m.flushed       = split.flushedBranch + split.flushedJump + split.flushedTrap;
    }
    if (hazardUnit)
    {
        m.stallsLoadUse         = split.stallLoadUse;
        m.stallsRawNoForwarding = split.stallRawNoForward;
        m.stalls     = p.total_stall_cycles_;
        m.stallsNote = "Cycles the hazard unit held the pipeline and put a bubble in EX.";
    }
    else if (singleCycle)
        m.stallsNote = "Single-cycle: every instruction finishes in its own cycle, so nothing stalls.";
    else
        m.stallsNote = "No hazard detection: this pipeline never stalls. Dependent "
                       "instructions need nops instead.";

    if (singleCycle)
        m.mispredictionsNote =
            "Single-cycle: the next PC is known before the next fetch, so nothing is predicted.";
    else
    {
        m.mispredictions     = p.branch_mispredictions_;
        m.mispredictionsNote = "Fetch assumes a branch is not taken. Each taken branch is a "
                               "misprediction and flushes the two instructions behind it.";
    }
    return m;
}

unsigned int ProcessorManager::getInstructionsRetired() const
{
    return m_currentProcessor->instructions_retired_;
}

const ExecutionTrace &ProcessorManager::trace() const
{
    return m_currentProcessor->Trace();
}

TraceMeta ProcessorManager::traceMeta() const
{
    TraceMeta meta;
    switch (m_currentProcessorType)
    {
    case ProcessorType::RVSS: meta.model = QStringLiteral("rvss"); break;
    case ProcessorType::RV5Stage_NH_NF: meta.model = QStringLiteral("nh_nf"); break;
    case ProcessorType::RV5Stage_H_NF: meta.model = QStringLiteral("h_nf"); break;
    case ProcessorType::RV5Stage_NH_F: meta.model = QStringLiteral("nh_f"); break;
    case ProcessorType::RV5Stage_H_F: meta.model = QStringLiteral("h_f"); break;
    default: meta.model = QStringLiteral("unknown"); break;
    }
    meta.singleCycle     = m_currentProcessorType == ProcessorType::RVSS;
    meta.hazardDetection = m_currentProcessorType == ProcessorType::RV5Stage_H_NF ||
                           m_currentProcessorType == ProcessorType::RV5Stage_H_F;
    meta.forwarding = m_currentProcessorType == ProcessorType::RV5Stage_NH_F ||
                      m_currentProcessorType == ProcessorType::RV5Stage_H_F;
    meta.capacity = trace().capacity();
    meta.dropped  = trace().dropped();
    return meta;
}

QJsonObject ProcessorManager::traceJson() const
{
    return traceToJson(trace().all(), traceMeta(), m_currentProgram);
}

bool ProcessorManager::exportTraceJson(const QString &path, QString *error) const
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    const QByteArray bytes = QJsonDocument(traceJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

}//namespace Atlas
