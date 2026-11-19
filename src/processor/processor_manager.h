/**
 * @file processor_manager.h
 * @brief This file contains the declaration of the ProcessorManager class
 */

#pragma once

#include "common/assembled_program.h"
#include "config/config.h"
#include "processor/processor_base.h"
#include "processor/trace_json.h"
#include "processor_types.h"
#include "profiler/profiler.h"
#include "ui/processor_tab/circuit_scene.h"
#include "processor_state.h"
#include <QMutex>
#include <QObject>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace Kites
{
class RVSSProcessor;        
class RV5StageProcessorNHNF;
class RV5StageProcessorHNF; 
class RV5StageProcessorNHF; 
class RV5StageProcessorHF; 

/**
 * @brief This class is responsible for the management of the processor instance
 */
/// Everything the Processor tab's state table and the Profiler show about
/// the run, read in one place so the two cannot disagree.
struct ProcessorMetrics
{
    unsigned int cycles{};
    unsigned int retired{};
    double       cpi{};
    double       ipc{};
    /// Empty = this model has no such counter; the note says why.
    std::optional<unsigned int> stalls;
    std::optional<unsigned int> mispredictions;
    const char                 *stallsNote{""};
    const char                 *mispredictionsNote{""};
    /// WP-05, from the trace. The stall split is there when stalls is
    /// (load-use + RAW without forwarding == stalls); the flush split on
    /// every pipeline (branch + jump + CSR/ecall == flushed).
    std::optional<unsigned int> stallsLoadUse;
    std::optional<unsigned int> stallsRawNoForwarding;
    std::optional<unsigned int> flushed;
    std::optional<unsigned int> flushedBranch;
    std::optional<unsigned int> flushedJump;
    std::optional<unsigned int> flushedTrap;
};

class ProcessorManager : public QObject
{
    Q_OBJECT
public:
    ProcessorManager(QObject *parent = nullptr, ProcessorType vmType = ProcessorType::RVSS);
    void changeProcessor(ProcessorType vmType);
    ProcessorType getProcessorType();
    /**@brief Stops the worker and resets. @return false on timeout, in
     * which case NOTHING was cleared and the caller must not load or run.
     */
    [[nodiscard]] bool reset();
    void loadProgram(const AssembledProgram &program);
    void loadProgram(const std::string &sourceText);
    void run();
    void step();
    void debugRun();
    /**@brief Stops the worker and resets. @return false on timeout. */
    bool stop();
    /// The program ended (ran off its end and drained, or exited) and its
    /// final state is still on the processor.
    bool programEnded() const;
    /// The next manual step would block waiting for ecall read input.
    bool nextStepWaitsForInput();

    /**@brief Request a run from the UI thread. Snapshots the stop epoch
     * at REQUEST time and posts it with the invocation, so a stop issued
     * afterwards cancels this run even if it is already queued.
     */
    void requestRun();
    void requestDebugRun();
    void pause();
    void resume();
    void undo();
    void redo();

    void pushInput(const std::string &input);

    void setStepDelay(unsigned int delay);

    void setBreakpoints(const std::vector<uint64_t> &breakpoints);

    /**
     * @brief The editor's cursor is on this 1-based source line.
     *
     * The selected stages are the stages whose instruction comes from that
     * line - the inverse of the mapping that labels the editor
     * (updateEditorHighlight). A pseudo-instruction expanding to several
     * instructions can put the line in several stages; a line with nothing
     * in the pipeline selects none. RVSS has no stages and never selects.
     * Recomputed here, on every clock (step, Undo, Redo, run) and when the
     * processor or program changes; emitted as selectedStagesSignal.
     */
    void setSelectedSourceLine(int line);
    /// The current selection, a stageBit mask (WireStage IF..WB).
    quint8 selectedStages() const;

    RegisterFile *getRegisterFile() const;
    MemoryController *getMemoryController() const;
    CircuitScene *getCircuitScene() const;
    const Profiler* getProfiler() const;

    // gui calls these functions to get the current state of the vm
    // maybe instead we should bundle the stats in a struct and the return just 
    // that
    uint64_t getProgramCounter() const;
    float getCPI() const;
    float getIPC() const;
    unsigned int getBranchMispredictions() const;
    unsigned int getStallCycles() const;
    unsigned int getCycles() const;
    unsigned int getInstructionsRetired() const;
    /// The run's counters as the window shows them. Stalls are the
    /// cumulative hazard-unit stall cycles (not the countdown
    /// getStallCycles returns, known_issues 80).
    ProcessorMetrics metrics() const;

    /// WP-02: the current processor's per-cycle trace (thread-safe reads).
    const ExecutionTrace &trace() const;
    /// The model's name and flags as the JSON export states them.
    TraceMeta traceMeta() const;
    /// The whole trace as schema "atlas.trace/1" (trace_json.h).
    QJsonObject traceJson() const;
    /// Writes traceJson() to path. On failure returns false and says why.
    bool exportTraceJson(const QString &path, QString *error = nullptr) const;
    /// The program the trace's PCs refer to (source lines and text).
    const AssembledProgram &currentProgram() const { return m_currentProgram; }
private:
    void updateEditorHighlight(const std::vector<uint64_t>& programCounters);
    /// Recomputes the selection from the stored line and PCs and emits it.
    void refreshSelectedStages();

    // Written on the worker thread during Run (processorClockedSlot is a
    // DirectConnection) and on the UI thread by the editor, hence the mutex.
    mutable QMutex        m_selectionMutex;
    std::vector<uint64_t> m_lastProgramCounters;
    int                   m_selectedSourceLine{-1};
    quint8                m_selectedStages{0};
    // Maps the current program counter back to a 1-based editor source line,
    // or -1 if it isn't in the current program's mapping.
    int resolveCurrentSourceLine() const;

    AssembledProgram m_currentProgram{};
    std::unique_ptr<ProcessorBase> m_currentProcessor{};
    ProcessorType m_currentProcessorType;
    Profiler m_profiler{};
    // we need this as when we chage vm we need preserve the step delay
    unsigned int m_stepDelayMs{1000};
public slots:
    void runSlotEpoch(quint64 epoch);
    void debugRunSlotEpoch(quint64 epoch);
    void runSlot();
    // same as runSlot but the processor starts paused so the user can step from the first instruction
    void debugRunSlot();
    void processorClockedSlot(const ProcessorState &processorState);

signals:
    //this just forwards the signal from the current processor to the gui
    void processorClockedSignal(const ProcessorState &processorState);
    void runFinishedSignal();
    void processorStateChangedSignal();
    void processorPausedAtBreakpointSignal();
    void programOutputSignal(const QString &text);
    void inputRequestedSignal();
    // sourceLine is 1-based, or -1 if the failing PC couldn't be mapped back to a source line.
    void runErrorSignal(const QString &errorMessage, int sourceLine);
    void updateDisassemblySignal(const QString &disassemblyText);
    /// The stages selected from the editor's cursor line (stageBit mask).
    void selectedStagesSignal(quint8 stages);
    void updateEditorHighlightSignal(const std::vector<std::pair<int,std::string>> &editorLines, 
        const std::vector<std::pair<int,std::string>> &disassemblyLines);
    // std::unique_ptr<> m_instance;
};
}//namespace Kites
