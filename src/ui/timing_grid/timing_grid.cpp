#include "ui/timing_grid/timing_grid.h"

#include "processor/processor_manager.h"
#include "processor/trace_json.h"
#include "ui/theme/theme_manager.h"

#include <QAction>
#include <QApplication>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace Kites
{
namespace
{
constexpr int kCellW   = 36;
constexpr int kCellH   = 22;
constexpr int kHeaderH = 22;

const char *kShort[] = {"IF", "ID", "EX", "ME", "WB"};
const char *kLong[]  = {"IF", "ID", "EX", "MEM", "WB"};

QColor blend(const QColor &top, const QColor &bottom, double alpha)
{
    return QColor::fromRgbF(top.redF() * alpha + bottom.redF() * (1 - alpha),
                            top.greenF() * alpha + bottom.greenF() * (1 - alpha),
                            top.blueF() * alpha + bottom.blueF() * (1 - alpha));
}

double luminance(const QColor &c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

QColor surface()
{
    return QApplication::palette().color(QPalette::Base);
}
} // namespace

double contrastRatio(const QColor &a, const QColor &b)
{
    const double la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

const char *timingStageToken(TraceStage stage)
{
    switch (stage)
    {
    case TraceStage::IF: return "info";
    case TraceStage::ID: return "success";
    case TraceStage::EX: return "warning";
    case TraceStage::MEM: return "cacheHit";
    case TraceStage::WB: return "foreground";
    }
    return "foreground";
}

TimingCellColors timingCellColorsFrom(const QColor &accent, const QColor &surfaceColor, const QColor &text)
{
    // A light tint keeps the theme's own text readable on every stage; the
    // stage is told apart by the accent bar and the label as well as hue.
    return {blend(accent, surfaceColor, 0.22), accent, text};
}

TimingCellColors timingCellColors(TraceStage stage)
{
    const ThemeManager &t = ThemeManager::getInstance();
    QColor              accent;
    switch (stage)
    {
    case TraceStage::IF: accent = t.getInfoColor(); break;
    case TraceStage::ID: accent = t.getSuccessColor(); break;
    case TraceStage::EX: accent = t.getWarningColor(); break;
    case TraceStage::MEM: accent = t.getCacheHitColor(); break;
    case TraceStage::WB: accent = t.getTextColor(); break;
    }
    return timingCellColorsFrom(accent, surface(), t.getTextColor());
}

TimingGridData buildTimingGrid(const std::vector<TraceCycle> &records, const AssembledProgram &program)
{
    TimingGridData grid;
    grid.records = records;
    if (records.empty())
        return grid;
    grid.singleCycle = records.front().singleCycle;
    grid.firstCycle  = records.front().cycle;
    std::map<uint32_t, int> rowOf;
    const int               columns = static_cast<int>(records.size());
    for (int c = 0; c < columns; ++c)
    {
        const TraceCycle &r = records[static_cast<size_t>(c)];
        for (size_t s = 0; s < kTraceStages; ++s)
        {
            const TraceOccupant &o = r.occ[s];
            if (o.bubble())
                continue;
            auto it = rowOf.find(o.id);
            if (it == rowOf.end())
            {
                TimingRow row;
                row.id    = o.id;
                row.pc    = o.pc;
                row.label = traceDisassembly(program, o.pc);
                if (row.label.isEmpty())
                    row.label = QStringLiteral("0x%1").arg(o.pc, 0, 16);
                row.cells.resize(columns);
                it = rowOf.emplace(o.id, static_cast<int>(grid.rows.size())).first;
                grid.rows.push_back(row);
            }
            TimingRow  &row  = grid.rows[it->second];
            TimingCell &cell = row.cells[c];
            cell.stage       = static_cast<TraceStage>(s);
            cell.kind        = TimingCell::Stage;
            if (s == 1 && r.stalled)
                cell.kind = TimingCell::Stalled;
            // IF held: the same fetch is still in IF next cycle.
            if (s == 0 && r.postIds[0] == o.id)
                cell.kind = TimingCell::Held;
            if (std::find(r.redirect.squashed.begin(), r.redirect.squashed.end(), o) !=
                r.redirect.squashed.end())
            {
                cell.kind    = TimingCell::Squashed;
                row.squashed = true;
            }
        }
    }
    return grid;
}

QStringList timingGridDiagram(const TimingGridData &grid)
{
    QStringList out;
    for (const TimingRow &row : grid.rows)
    {
        QStringList tokens;
        for (const TimingCell &cell : row.cells)
        {
            if (cell.kind == TimingCell::Empty)
            {
                tokens << QStringLiteral(".");
                continue;
            }
            QString t = QString::fromLatin1(kShort[static_cast<int>(cell.stage)]);
            if (cell.kind == TimingCell::Stalled)
                t += '*';
            if (cell.kind == TimingCell::Squashed)
                t += 'x';
            tokens << t;
        }
        while (!tokens.isEmpty() && tokens.back() == QLatin1String("."))
            tokens.removeLast();
        out << tokens.join(' ');
    }
    return out;
}

// ---- View ------------------------------------------------------------------
TimingGridView::TimingGridView(QWidget *parent) : QWidget(parent)
{
    setObjectName("timingGridView");
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setMouseTracking(false);
}

int TimingGridView::labelWidth() const
{
    int w = 120;
    for (const TimingRow &row : m_grid.rows)
        w = std::max(w, fontMetrics().horizontalAdvance(row.label) + 16);
    return std::min(w, 260);
}

QSize TimingGridView::sizeHint() const
{
    return {labelWidth() + m_grid.columns() * kCellW + 2,
            kHeaderH + static_cast<int>(m_grid.rows.size()) * kCellH + 2};
}

void TimingGridView::setGrid(const TimingGridData &grid)
{
    m_grid = grid;
    if (m_selRow >= m_grid.rows.size() || m_selCol >= m_grid.columns())
        m_selRow = m_selCol = -1;
    resize(sizeHint());
    setMinimumSize(sizeHint());
    update();
}

void TimingGridView::select(int row, int column)
{
    if (row < 0 || column < 0 || row >= m_grid.rows.size() || column >= m_grid.columns())
        return;
    m_selRow = row;
    m_selCol = column;
    update();
    emit cellSelected(row, column);
}

void TimingGridView::mousePressEvent(QMouseEvent *event)
{
    const QPoint p   = event->position().toPoint();
    const int    col = (p.x() - labelWidth()) / kCellW;
    const int    row = (p.y() - kHeaderH) / kCellH;
    if (p.x() >= labelWidth() && p.y() >= kHeaderH)
        select(row, col);
}

void TimingGridView::paintEvent(QPaintEvent *)
{
    QPainter            p(this);
    const ThemeManager &theme = ThemeManager::getInstance();
    const QColor        base  = surface();
    const QColor        text  = theme.getTextColor();
    const QColor        muted = QApplication::palette().color(QPalette::PlaceholderText);
    const QColor        grid  = blend(text, base, 0.15);
    p.fillRect(rect(), base);
    const int lw   = labelWidth();
    const int cols = m_grid.columns();

    // The current cycle (the last recorded one) is highlighted.
    if (cols > 0)
        p.fillRect(QRect(lw + (cols - 1) * kCellW, 0, kCellW, height()), theme.getCurrentLineColor());

    // Header: cycle numbers.
    p.setPen(muted);
    for (int c = 0; c < cols; ++c)
        p.drawText(QRect(lw + c * kCellW, 0, kCellW, kHeaderH), Qt::AlignCenter,
                   QString::number(m_grid.firstCycle + static_cast<uint32_t>(c)));

    for (int r = 0; r < m_grid.rows.size(); ++r)
    {
        const TimingRow &row = m_grid.rows[r];
        const int        y   = kHeaderH + r * kCellH;
        p.setPen(row.squashed ? muted : text);
        QFont f = font();
        f.setStrikeOut(row.squashed); // a flushed instruction is struck through
        p.setFont(f);
        p.drawText(QRect(6, y, lw - 10, kCellH), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(row.label, Qt::ElideRight, lw - 10));
        p.setFont(font());
        for (int c = 0; c < cols; ++c)
        {
            const TimingCell &cell = row.cells[c];
            const QRect       box(lw + c * kCellW + 1, y + 1, kCellW - 2, kCellH - 2);
            if (cell.kind == TimingCell::Empty)
                continue;
            const TimingCellColors colors = timingCellColors(cell.stage);
            p.fillRect(box, colors.fill);
            p.fillRect(QRect(box.left(), box.top(), 3, box.height()), colors.accent);
            if (cell.kind == TimingCell::Stalled || cell.kind == TimingCell::Held)
            {
                // A stall bubble: hatched.
                QBrush hatch(blend(theme.getWarningColor(), base, 0.55), Qt::BDiagPattern);
                p.fillRect(box, hatch);
            }
            p.setPen(colors.text);
            QString label = QString::fromLatin1(kLong[static_cast<int>(cell.stage)]);
            if (cell.kind == TimingCell::Stalled)
                label += '*';
            p.drawText(box, Qt::AlignCenter, label);
            if (cell.kind == TimingCell::Squashed)
            {
                // Flushed: crossed out in the theme's error colour.
                p.setPen(QPen(theme.getErrorColor(), 2));
                p.drawLine(box.topLeft(), box.bottomRight());
                p.drawLine(box.bottomLeft(), box.topRight());
            }
        }
        p.setPen(grid);
        p.drawLine(0, y + kCellH, lw + cols * kCellW, y + kCellH);
    }
    if (m_selRow >= 0 && m_selCol >= 0)
    {
        p.setPen(QPen(theme.getInfoColor(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(lw + m_selCol * kCellW, kHeaderH + m_selRow * kCellH, kCellW, kCellH));
    }
}

// ---- Panel -----------------------------------------------------------------
TimingGridPanel::TimingGridPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName("timingGridPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);

    auto *bar  = new QHBoxLayout;
    m_stepBack = new QToolButton(this);
    m_stepBack->setObjectName("timingStepBack");
    bar->addWidget(m_stepBack);
    bar->addSpacing(12);
    bar->addWidget(new QLabel("Jump to cycle", this));
    m_jumpSpin = new QSpinBox(this);
    m_jumpSpin->setObjectName("timingJumpSpin");
    m_jumpSpin->setRange(0, 10000000);
    bar->addWidget(m_jumpSpin);
    m_jumpGo = new QPushButton("Go", this);
    m_jumpGo->setObjectName("timingJumpGo");
    bar->addWidget(m_jumpGo);
    bar->addStretch(1);
    layout->addLayout(bar);

    m_message = new QLabel(this);
    m_message->setObjectName("timingMessage");
    m_message->setWordWrap(true);
    layout->addWidget(m_message);

    m_view   = new TimingGridView;
    m_scroll = new QScrollArea(this);
    m_scroll->setWidget(m_view);
    m_scroll->setWidgetResizable(false);
    m_scroll->setMinimumHeight(140);
    layout->addWidget(m_scroll, 1);

    m_explain = new QTextBrowser(this);
    m_explain->setObjectName("timingExplanation");
    m_explain->setMaximumHeight(56);
    m_explain->setPlaceholderText("Select a cell to see what that instruction did in that cycle.");
    layout->addWidget(m_explain);

    connect(m_jumpGo, &QPushButton::clicked, this,
            [this]() { jumpToCycle(static_cast<uint32_t>(m_jumpSpin->value())); });
    connect(m_view, &TimingGridView::cellSelected, this, &TimingGridPanel::explainSelection);
    refresh();
}

void TimingGridPanel::setUp(ProcessorManager *manager, QAction *stepAction, QAction *undoAction)
{
    m_manager    = manager;
    m_stepAction = stepAction;
    m_undoAction = undoAction;
    m_stepBack->setDefaultAction(undoAction); // step back IS Undo: the same action
    m_stepBack->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    if (manager)
        connect(manager, &ProcessorManager::processorStateChangedSignal, this, &TimingGridPanel::refresh,
                Qt::QueuedConnection);
    auto sync = [this]()
    {
        const bool canMove = (m_stepAction && m_stepAction->isEnabled()) ||
                             (m_undoAction && m_undoAction->isEnabled());
        m_jumpGo->setEnabled(canMove && !jumping());
    };
    if (stepAction)
        connect(stepAction, &QAction::changed, this, sync);
    if (undoAction)
        connect(undoAction, &QAction::changed, this, sync);
    sync();
    refresh();
}

uint32_t TimingGridPanel::currentCycle() const
{
    return m_manager ? m_manager->getCycles() : 0;
}

QString TimingGridPanel::message() const
{
    return m_message->text();
}

QString TimingGridPanel::explanation() const
{
    return m_explain->toPlainText();
}

void TimingGridPanel::refresh()
{
    if (!m_manager)
    {
        m_message->setText("No processor.");
        return;
    }
    const ExecutionTrace &trace = m_manager->trace();
    const TraceMeta       meta  = m_manager->traceMeta();
    if (meta.singleCycle)
    {
        m_message->setText("Single-cycle processor: every instruction runs from fetch to "
                           "write-back in one cycle, so there is no pipeline timing to draw. "
                           "Choose a 5-stage processor (Processor...) to see the grid.");
        m_message->show();
        m_scroll->hide();
        m_view->setGrid({});
        return;
    }
    m_scroll->show();
    const uint32_t last  = trace.lastCycle();
    const uint32_t first = last > kVisibleCycles ? last - kVisibleCycles + 1 : 1;
    const auto     recs  = last ? trace.window(first, kVisibleCycles) : std::vector<TraceCycle>{};
    m_view->setGrid(buildTimingGrid(recs, m_manager->currentProgram()));
    if (recs.empty())
        m_message->setText("Step or run a program to draw its timing.");
    else
        m_message->setText(QStringLiteral("Cycle %1 of the run. Each row is one fetched instruction; "
                                          "* = stalled, hatched = bubble, crossed = flushed.")
                               .arg(last));
    m_scroll->horizontalScrollBar()->setValue(m_scroll->horizontalScrollBar()->maximum());
}

void TimingGridPanel::jumpToCycle(uint32_t cycle)
{
    if (!m_manager || jumping())
        return;
    m_jumpTarget = cycle;
    m_jumpGo->setEnabled(false);
    QTimer::singleShot(0, this, &TimingGridPanel::jumpChunk);
}

void TimingGridPanel::jumpChunk()
{
    if (!m_jumpTarget || !m_manager)
        return;
    const uint32_t target = *m_jumpTarget;
    // A bounded number of moves per event-loop turn: the window repaints and
    // stays responsive between chunks, however far the jump.
    for (int i = 0; i < 200; ++i)
    {
        const uint32_t now    = m_manager->getCycles();
        QAction       *action = now > target ? m_undoAction.data() : now < target ? m_stepAction.data() : nullptr;
        if (!action || !action->isEnabled())
        {
            m_jumpTarget.reset();
            m_jumpGo->setEnabled(true);
            refresh();
            emit jumpFinished(m_manager->getCycles());
            return;
        }
        action->trigger();
        if (m_manager->getCycles() == now)
        {
            // Nothing moved (the program's end, or a read waiting for input).
            m_jumpTarget.reset();
            m_jumpGo->setEnabled(true);
            refresh();
            emit jumpFinished(now);
            return;
        }
    }
    QTimer::singleShot(0, this, &TimingGridPanel::jumpChunk);
}

void TimingGridPanel::explainSelection(int row, int column)
{
    const TimingGridData &g = m_view->grid();
    if (row < 0 || column < 0 || row >= g.rows.size() || column >= g.columns())
        return;
    const TimingRow  &r    = g.rows[row];
    const TimingCell &cell = r.cells[column];
    const uint32_t    cyc  = g.firstCycle + static_cast<uint32_t>(column);
    if (cell.kind == TimingCell::Empty)
        m_explain->setPlainText(QStringLiteral("Cycle %1: %2 is not in the pipeline.").arg(cyc).arg(r.label));
    else
        m_explain->setPlainText(QStringLiteral("Cycle %1: %2 is in %3.")
                                    .arg(cyc)
                                    .arg(r.label)
                                    .arg(QString::fromLatin1(kLong[static_cast<int>(cell.stage)])));
}
} // namespace Kites
