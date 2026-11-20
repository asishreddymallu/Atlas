/**
 * @file timing_grid.h
 * @brief WP-03: the multi-cycle pipeline timing diagram (instructions down,
 *        clock cycles across), drawn from the ExecutionTrace.
 *
 * The grid is a view of the trace and nothing else: it never asks the
 * processor anything, so it cannot show a stage, a stall or a flush the
 * trace did not record. Moving through time (step back, jump to cycle) goes
 * through the window's own Step and Undo actions, i.e. the real machine.
 */
#pragma once

#include "common/assembled_program.h"
#include "processor/execution_trace.h"

#include <QAbstractScrollArea>
#include <QColor>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <deque>
#include <optional>
#include <vector>

class QAction;
class QLabel;
class QPushButton;
class QSpinBox;
class QTextBrowser;
class QToolButton;

namespace Kites
{
class ProcessorManager;
struct TraceMeta;

/// One cell: what an instruction did in one cycle.
struct TimingCell
{
    enum Kind : quint8
    {
        Empty,
        Stage,   ///< in stage `stage`
        Stalled, ///< in ID, held by the hazard unit this cycle
        Held,    ///< in IF, its fetch held by a stall behind it
        Squashed ///< in stage `stage`, flushed by a redirect this cycle
    };
    Kind       kind  = Empty;
    TraceStage stage = TraceStage::IF;
};

/// One fetched instruction. Its cells cover the cycles it was in the
/// pipeline: cells[i] is cycle firstCycle + i (a handful per row, so a long
/// run costs rows x lifetime, not rows x columns).
struct TimingRow
{
    uint32_t            id = 0;
    uint64_t            pc = 0;
    QString             label; ///< the disassembly
    bool                squashed = false;
    uint32_t            firstCycle = 0; ///< the cycle of cells[0]
    QVector<TimingCell> cells;
    /// The cell in `cycle` (Empty outside the row's cycles).
    TimingCell at(uint32_t cycle) const
    {
        if (cycle < firstCycle || cycle - firstCycle >= static_cast<uint32_t>(cells.size()))
            return {};
        return cells[static_cast<qsizetype>(cycle - firstCycle)];
    }
    uint32_t lastCycle() const { return firstCycle + static_cast<uint32_t>(cells.size()) - 1; }
};

/// The grid: columns are the cycles firstCycle..lastCycle, rows are the
/// instructions seen in them, in fetch order (increasing instance id).
struct TimingGridData
{
    bool             singleCycle = false;
    uint32_t         firstCycle  = 0; ///< the cycle of column 0 (0 = empty grid)
    uint32_t         lastCycle   = 0; ///< the cycle of the last column, the current one
    QList<TimingRow> rows;
    int columns() const { return firstCycle ? static_cast<int>(lastCycle - firstCycle + 1) : 0; }
    TimingCell cell(int row, int column) const
    {
        return rows[row].at(firstCycle + static_cast<uint32_t>(column));
    }
    /// The row of instance `id`, or -1 (binary search: rows are in id order).
    int rowOfId(uint32_t id) const;
};

/**
 * @brief The grid kept in step with an ExecutionTrace, incrementally.
 *
 * sync() compares the grid with the trace at the last cycle they share
 * (one record) and then only appends the new cycles, cuts the cycles an
 * Undo removed, and drops the ones the ring buffer let go. Anything else
 * (a reset, a new program, a different history) rebuilds from the trace.
 */
class TimingGridModel
{
  public:
    void clear();
    /// Brings the grid in line with `trace`; true when anything changed.
    bool sync(const ExecutionTrace &trace, const AssembledProgram &program);
    /// Adds the next cycle (its cycle must be lastCycle + 1, or the first).
    void append(const TraceCycle &record, const AssembledProgram &program);
    const TimingGridData &data() const { return m_data; }
    /// Every label met so far (for the label column's width).
    const QHash<quint64, QString> &labels() const { return m_labels; }

  private:
    void           truncateAfter(uint32_t lastKept);
    void           dropBefore(uint32_t first);
    void           rebuild(const ExecutionTrace &trace, const AssembledProgram &program);
    const QString &labelOf(const TraceOccupant &o, const AssembledProgram &program);

    TimingGridData          m_data;
    std::deque<uint64_t>    m_prints;      ///< a fingerprint of each column's record
    QHash<quint64, QString> m_labels;      ///< by pc and instruction word
    int                     m_maxSpan = 0; ///< the longest row seen (cells)
};

/// The grid for these records (consecutive cycles), rows in fetch order.
TimingGridData buildTimingGrid(const std::vector<TraceCycle> &records, const AssembledProgram &program);

/// The grid in the notation of the hand-drawn diagrams (execution_trace_test.cpp):
/// IF ID EX ME WB, ID* stalled, IFx/IDx/EXx/MEx squashed, . empty; trailing dots dropped.
QStringList timingGridDiagram(const TimingGridData &grid);

/// A stage cell's colours, from the active theme's tokens: a tint of the
/// stage's token over the surface, the theme's text on top.
struct TimingCellColors
{
    QColor fill;
    QColor accent;
    QColor text;
};
TimingCellColors timingCellColors(TraceStage stage);
/// The theme token a stage is coloured from: info, success, warning,
/// cacheHit, foreground (the names in resources/themes/*.json).
const char *timingStageToken(TraceStage stage);
/// The rule behind timingCellColors, on explicit colours (for the AA test).
TimingCellColors timingCellColorsFrom(const QColor &accent, const QColor &surface, const QColor &text);
/// WCAG 2 contrast ratio of two opaque colours.
double contrastRatio(const QColor &a, const QColor &b);

/// The painted grid: only the visible cells are drawn, with the cycle
/// numbers and the instruction labels pinned while it scrolls. Click a cell
/// to select it.
class TimingGridView : public QAbstractScrollArea
{
    Q_OBJECT
  public:
    explicit TimingGridView(QWidget *parent = nullptr);
    /// The grid this view draws (the panel keeps it in step with the trace).
    TimingGridModel      &model() { return m_model; }
    const TimingGridData &grid() const { return m_model.data(); }
    /// After the model changed: the selection is dropped (the panel puts it
    /// back), the scroll range follows, and the current cycle is shown.
    void gridChanged();
    /// Scrolls so the current cycle (the last column) and the newest rows show.
    void followCurrentCycle();
    void select(int row, int column);
    /// Drops the selection without explaining anything.
    void clearSelection();
    /// Puts a selection back after gridChanged, without emitting cellSelected.
    void restoreSelection(int row, int column);
    int  selectedRow() const { return m_selRow; }
    int  selectedColumn() const { return m_selCol; }
    QSize sizeHint() const override;

  signals:
    void cellSelected(int row, int column);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    void updateScrollRange();
    int  labelWidth() const { return m_labelWidth; }

    TimingGridModel m_model;
    int             m_selRow       = -1;
    int             m_selCol       = -1;
    int             m_labelWidth   = 120;
    qsizetype       m_labelsMeasured = 0;
};

/// The dock's contents: controls, the grid and the explanation panel.
class TimingGridPanel : public QWidget
{
    Q_OBJECT
  public:
    /// Work per event-loop turn while jumping (one frame is ~16 ms).
    static constexpr int kJumpChunkMs = 8;

    explicit TimingGridPanel(QWidget *parent = nullptr);
    /// stepAction / undoAction / redoAction: the window's own actions (the
    /// real machine). A forward jump Redoes while it can, then Steps.
    void setUp(ProcessorManager *manager, QAction *stepAction, QAction *undoAction,
               QAction *redoAction = nullptr);

    /// Brings the grid up to date with the trace: every stored cycle, rows in
    /// fetch order. Incremental, so its cost is the cycles that changed. While
    /// the panel is hidden it only notes that it is behind (showEvent catches up).
    void refresh();
    /// Tests: read this trace instead of the machine's (nullptr: the machine's).
    void useTrace(const ExecutionTrace *trace);
    /// Steps or undoes until the machine is at `cycle`, a chunk per event-
    /// loop turn so the window keeps painting. Emits jumpFinished.
    void jumpToCycle(uint32_t cycle);
    bool jumping() const { return m_jumpTarget.has_value(); }
    /// The machine's cycle count (0 with no processor).
    uint32_t currentCycle() const;
    /// Forgets the selected cell and its sentence (a new Run, a reload, a
    /// model switch). refresh() also drops them once the cell is gone.
    void clearSelection();

    TimingGridView   *view() const { return m_view; }
    ProcessorManager *manager() const { return m_manager; }
    QString         message() const;
    QString         explanation() const;

  signals:
    void jumpFinished(uint32_t cycle);

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    const ExecutionTrace *trace() const;
    void scheduleRefresh();
    void jumpChunk();
    void explainSelection(int row, int column);
    void keepOrClearSelection(const TraceMeta &meta);

    /// The explained cell, by what it is rather than where it was drawn.
    struct SelectedCell
    {
        QString    model;
        uint32_t   id = 0;
        TraceCycle record;
    };
    std::optional<SelectedCell> m_selected;

    QPointer<ProcessorManager> m_manager;
    QPointer<QAction>          m_stepAction;
    QPointer<QAction>          m_undoAction;
    QPointer<QAction>          m_redoAction;
    QLabel                    *m_message   = nullptr;
    TimingGridView            *m_view      = nullptr;
    QToolButton               *m_stepBack  = nullptr;
    QSpinBox                  *m_jumpSpin  = nullptr;
    QPushButton               *m_jumpGo    = nullptr;
    QTextBrowser              *m_explain   = nullptr;
    std::optional<uint32_t>    m_jumpTarget;
    bool                       m_redoDrained = false; ///< this jump found nothing left to Redo
    const ExecutionTrace      *m_traceOverride = nullptr;
    bool                       m_refreshQueued = false;
    bool                       m_stale = false; ///< refresh skipped while hidden
};
} // namespace Kites
