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

namespace Kites
{
ProcessorManager::ProcessorManager(QObject *parent, ProcessorType vmType) : QObject(parent)
{
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
        qWarning("Kites: processor did not stop; not switching processor");
        return;
    }
    m_currentProcessorType = vmType;
    m_currentProcessor = ProcessorFactory::createVM(vmType);
    m_currentProcessor->step_delay_ = m_stepDelayMs;
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorStateChangedSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSlot, Qt::DirectConnection);        
    connect(m_currentProcessor.get(), &ProcessorBase::processorPausedAtBreakpointSignal, this,
            &ProcessorManager::processorPausedAtBreakpointSignal, Qt::DirectConnection);
    connect(m_currentProcessor.get(), &ProcessorBase::processorClockedSignal, this,
            &ProcessorManager::processorClockedSignal, Qt::DirectConnection);
    // create new connections for the new VM

    // The new processor has run nothing: no stage holds an instruction, so
    // nothing is selected until the next clock. The cursor line is kept.
    {
        QMutexLocker lock(&m_selectionMutex);
        m_lastProgramCounters.clear();
    }
    refreshSelectedStages();
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
    m_profiler.setInstructionToLineMapping(program);
    m_profiler.setLineNumberToInstructionTypeMapping(program);
    updateEditorHighlight({0});
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
    try
    {
        if (!m_currentProcessor->StepAndNotify())
        {
            // program finished while stepping, let the run thread exit so the ui resets
            m_currentProcessor->RequestStop();
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
    m_currentProcessor->Redo();
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

Kites::CircuitScene *ProcessorManager::getCircuitScene() const
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
        qWarning("Kites: the processor did not stop within the timeout; "
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

unsigned int ProcessorManager::getInstructionsRetired() const
{
    return m_currentProcessor->instructions_retired_;
}

}//namespace Kites
