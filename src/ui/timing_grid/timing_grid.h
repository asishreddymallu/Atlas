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

#include <QColor>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <optional>
#include <vector>

class QAction;
class QLabel;
class QPushButton;
class QScrollArea;
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

struct TimingRow
{
    uint32_t            id = 0;
    uint64_t            pc = 0;
    QString             label; ///< the disassembly
    bool                squashed = false;
    QVector<TimingCell> cells; ///< one per column
};

struct TimingGridData
{
    bool               singleCycle = false;
    uint32_t           firstCycle  = 0; ///< the cycle of column 0 (0 = empty grid)
    QVector<TimingRow> rows;
    std::vector<TraceCycle> records; ///< the columns' records, in order
    int columns() const { return static_cast<int>(records.size()); }
};

/// The grid for these records (consecutive cycles), rows in order of first fetch.
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

/// The painted grid. Click a cell to select it.
class TimingGridView : public QWidget
{
    Q_OBJECT
  public:
    explicit TimingGridView(QWidget *parent = nullptr);
    void setGrid(const TimingGridData &grid);
    const TimingGridData &grid() const { return m_grid; }
    void select(int row, int column);
    /// Drops the selection without explaining anything.
    void clearSelection();
    /// Puts a selection back after setGrid, without emitting cellSelected.
    void restoreSelection(int row, int column);
    int  selectedRow() const { return m_selRow; }
    int  selectedColumn() const { return m_selCol; }
    QSize sizeHint() const override;

  signals:
    void cellSelected(int row, int column);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

  private:
    TimingGridData m_grid;
    int            m_selRow = -1;
    int            m_selCol = -1;
    int            labelWidth() const;
};

/// The dock's contents: controls, the grid and the explanation panel.
class TimingGridPanel : public QWidget
{
    Q_OBJECT
  public:
    static constexpr int kVisibleCycles = 40;
    /// Work per event-loop turn while jumping (one frame is ~16 ms).
    static constexpr int kJumpChunkMs = 8;

    explicit TimingGridPanel(QWidget *parent = nullptr);
    /// stepAction / undoAction / redoAction: the window's own actions (the
    /// real machine). A forward jump Redoes while it can, then Steps.
    void setUp(ProcessorManager *manager, QAction *stepAction, QAction *undoAction,
               QAction *redoAction = nullptr);

    /// Re-reads the trace (the last kVisibleCycles cycles).
    void refresh();
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

  private:
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
    QScrollArea               *m_scroll    = nullptr;
    TimingGridView            *m_view      = nullptr;
    QToolButton               *m_stepBack  = nullptr;
    QSpinBox                  *m_jumpSpin  = nullptr;
    QPushButton               *m_jumpGo    = nullptr;
    QTextBrowser              *m_explain   = nullptr;
    std::optional<uint32_t>    m_jumpTarget;
    bool                       m_redoDrained = false; ///< this jump found nothing left to Redo
};
} // namespace Kites
