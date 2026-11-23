#include "ui/timing_grid/timing_grid.h"

#include "processor/processor_manager.h"
#include "processor/trace_explain.h"
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
#include <QLocale>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextBrowser>
#include <QElapsedTimer>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <map>

namespace Atlas
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

// ---- Model -----------------------------------------------------------------
namespace
{
/// Enough of a record to tell two histories apart at one cycle.
uint64_t fingerprint(const TraceCycle &r)
{
    uint64_t h   = 1469598103934665603ull;
    auto     mix = [&h](uint64_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(r.cycle);
    for (const TraceOccupant &o : r.occ)
    {
        mix(o.id);
        mix(o.pc);
        mix(o.instruction);
    }
    for (uint32_t id : r.postIds)
        mix(id);
    mix(r.stalled);
    mix(r.nextIdBefore);
    mix(r.nextIdAfter);
    mix(static_cast<uint64_t>(r.redirect.kind));
    for (const TraceOccupant &o : r.redirect.squashed)
        mix(o.id);
    return h;
}

void recomputeSquashed(TimingRow &row)
{
    row.squashed = std::any_of(row.cells.begin(), row.cells.end(),
                               [](const TimingCell &c) { return c.kind == TimingCell::Squashed; });
}
} // namespace

int TimingGridData::rowOfId(uint32_t id) const
{
    const auto it = std::lower_bound(rows.begin(), rows.end(), id,
                                     [](const TimingRow &r, uint32_t v) { return r.id < v; });
    return it != rows.end() && it->id == id ? static_cast<int>(it - rows.begin()) : -1;
}

void TimingGridModel::clear()
{
    m_data = {};
    m_prints.clear();
    m_labels.clear();
    m_maxSpan = 0;
}

const QString &TimingGridModel::labelOf(const TraceOccupant &o, const AssembledProgram &program)
{
    const quint64 key = (static_cast<quint64>(o.pc) << 32) ^ o.instruction;
    auto          it  = m_labels.find(key);
    if (it == m_labels.end())
    {
        QString label = traceDisassembly(program, o.pc);
        if (label.isEmpty())
            label = QStringLiteral("0x%1").arg(o.pc, 0, 16);
        it = m_labels.insert(key, label);
    }
    return it.value();
}

void TimingGridModel::append(const TraceCycle &r, const AssembledProgram &program)
{
    const uint32_t c = r.cycle;
    if (m_data.firstCycle == 0)
    {
        m_data.firstCycle  = c;
        m_data.singleCycle = r.singleCycle;
    }
    m_data.lastCycle = c;
    m_prints.push_back(fingerprint(r));
    QList<TimingRow> &rows = m_data.rows;
    for (size_t s = 0; s < kTraceStages; ++s)
    {
        const TraceOccupant &o = r.occ[s];
        if (o.bubble())
            continue;
        // Rows stay in id order, which is fetch order (one id per fetch).
        // A new fetch is the youngest, so it nearly always goes at the end.
        qsizetype idx;
        if (!rows.isEmpty() && rows.back().id == o.id)
            idx = rows.size() - 1;
        else if (rows.isEmpty() || rows.back().id < o.id)
            idx = rows.size();
        else
            idx = std::lower_bound(rows.begin(), rows.end(), o.id,
                                   [](const TimingRow &row, uint32_t v) { return row.id < v; }) -
                  rows.begin();
        if (idx == rows.size() || rows[idx].id != o.id)
        {
            TimingRow row;
            row.id         = o.id;
            row.pc         = o.pc;
            row.label      = labelOf(o, program);
            row.firstCycle = c;
            rows.insert(idx, row);
        }
        TimingRow      &row = rows[idx];
        const qsizetype off = static_cast<qsizetype>(c - row.firstCycle);
        if (row.cells.size() <= off)
            row.cells.resize(off + 1);
        m_maxSpan        = std::max(m_maxSpan, static_cast<int>(row.cells.size()));
        TimingCell &cell = row.cells[off];
        cell.stage       = static_cast<TraceStage>(s);
        cell.kind        = TimingCell::Stage;
        if (s == 1 && r.stalled)
            cell.kind = TimingCell::Stalled;
        // IF held: the same fetch is still in IF next cycle.
        if (s == 0 && r.postIds[0] == o.id)
            cell.kind = TimingCell::Held;
        if (std::find(r.redirect.squashed.begin(), r.redirect.squashed.end(), o) != r.redirect.squashed.end())
        {
            cell.kind    = TimingCell::Squashed;
            row.squashed = true;
        }
    }
}

void TimingGridModel::truncateAfter(uint32_t lastKept)
{
    QList<TimingRow> &rows = m_data.rows;
    // Rows are in fetch order, so the first cycles never decrease down the
    // list: the rows fetched after lastKept are a tail.
    while (!rows.isEmpty() && rows.back().firstCycle > lastKept)
        rows.removeLast();
    for (qsizetype i = rows.size() - 1; i >= 0; --i)
    {
        TimingRow &row = rows[i];
        // No row lives longer than m_maxSpan cycles, and the rows above
        // started no later: none of them reaches past lastKept.
        if (row.firstCycle + static_cast<uint32_t>(m_maxSpan) <= lastKept)
            break;
        if (row.lastCycle() > lastKept)
        {
            row.cells.resize(static_cast<qsizetype>(lastKept - row.firstCycle + 1));
            recomputeSquashed(row);
        }
    }
    while (m_data.lastCycle > lastKept)
    {
        m_prints.pop_back();
        --m_data.lastCycle;
    }
}

void TimingGridModel::dropBefore(uint32_t first)
{
    QList<TimingRow> &rows = m_data.rows;
    // The rows that started before `first` are a head; trim them, and drop
    // the ones with nothing left. The kept ones are packed against the rest
    // so the head comes off the front of the list in one cheap erase.
    qsizetype head = 0;
    while (head < rows.size() && rows[head].firstCycle < first)
        ++head;
    qsizetype keep = head;
    for (qsizetype i = head - 1; i >= 0; --i)
    {
        TimingRow &row = rows[i];
        if (row.lastCycle() < first)
            continue;
        row.cells.remove(0, static_cast<qsizetype>(first - row.firstCycle));
        row.firstCycle = first;
        recomputeSquashed(row);
        --keep;
        if (keep != i)
            rows[keep] = std::move(row);
    }
    rows.erase(rows.begin(), rows.begin() + keep);
    while (m_data.firstCycle < first)
    {
        m_prints.pop_front();
        ++m_data.firstCycle;
    }
}

void TimingGridModel::rebuild(const ExecutionTrace &trace, const AssembledProgram &program)
{
    clear();
    for (const TraceCycle &r : trace.all())
        append(r, program);
}

bool TimingGridModel::sync(const ExecutionTrace &trace, const AssembledProgram &program)
{
    const uint32_t tFirst = trace.firstCycle();
    const uint32_t tLast  = trace.lastCycle();
    if (tLast == 0)
    {
        if (m_data.firstCycle == 0)
            return false;
        clear();
        return true;
    }
    const bool have = m_data.firstCycle != 0;
    const uint32_t shared = have ? std::min(m_data.lastCycle, tLast) : 0;
    bool same = have && tFirst >= m_data.firstCycle && shared >= tFirst;
    if (same)
    {
        // The two histories agree at the last cycle they share.
        const std::vector<TraceCycle> at = trace.window(shared, 1);
        same = at.size() == 1 && at.front().cycle == shared &&
               fingerprint(at.front()) == m_prints[shared - m_data.firstCycle];
    }
    if (!same)
    {
        rebuild(trace, program);
        return true;
    }
    bool changed = false;
    if (shared < m_data.lastCycle)
    {
        truncateAfter(shared); // Undo
        changed = true;
    }
    if (tLast > shared)
    {
        for (const TraceCycle &r : trace.window(shared + 1, tLast - shared))
            if (r.cycle == m_data.lastCycle + 1)
                append(r, program);
        changed = true;
    }
    const uint32_t front = trace.firstCycle(); // the buffer may have moved on meanwhile
    if (front > m_data.firstCycle && front <= m_data.lastCycle)
    {
        dropBefore(front); // the ring buffer let these go
        changed = true;
    }
    return changed;
}

TimingGridData buildTimingGrid(const std::vector<TraceCycle> &records, const AssembledProgram &program)
{
    TimingGridModel model;
    for (const TraceCycle &r : records)
        model.append(r, program);
    return model.data();
}

QStringList timingGridDiagram(const TimingGridData &grid)
{
    QStringList out;
    for (const TimingRow &row : grid.rows)
    {
        QStringList tokens;
        for (int c = 0; c < grid.columns(); ++c)
        {
            const TimingCell cell = row.at(grid.firstCycle + static_cast<uint32_t>(c));
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
TimingGridView::TimingGridView(QWidget *parent) : QAbstractScrollArea(parent)
{
    setObjectName("timingGridView");
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    viewport()->setFont(font());
    setMouseTracking(false);
    horizontalScrollBar()->setSingleStep(kCellW);
    verticalScrollBar()->setSingleStep(kCellH);
}

QSize TimingGridView::sizeHint() const
{
    const TimingGridData &g = grid();
    const int w = labelWidth() + std::min(g.columns(), 40) * kCellW + 2 + 2 * frameWidth();
    const int h = kHeaderH + static_cast<int>(std::min<qsizetype>(g.rows.size(), 12)) * kCellH + 2 +
                  2 * frameWidth();
    return {w, h};
}

void TimingGridView::updateScrollRange()
{
    const TimingGridData &g     = grid();
    const QSize           vp    = viewport()->size();
    const qint64          fullW = static_cast<qint64>(g.columns()) * kCellW;
    const qint64          fullH = static_cast<qint64>(g.rows.size()) * kCellH;
    const int             viewW = std::max(0, vp.width() - labelWidth());
    const int             viewH = std::max(0, vp.height() - kHeaderH);
    horizontalScrollBar()->setRange(0, static_cast<int>(std::max<qint64>(0, fullW - viewW)));
    horizontalScrollBar()->setPageStep(viewW);
    verticalScrollBar()->setRange(0, static_cast<int>(std::max<qint64>(0, fullH - viewH)));
    verticalScrollBar()->setPageStep(viewH);
}

void TimingGridView::gridChanged()
{
    // A changed grid has no selection; the panel puts it back when the same
    // cell of the same trace is still drawn (TimingGridPanel::refresh).
    m_selRow = m_selCol = -1;
    const auto &labels = m_model.labels();
    if (labels.isEmpty())
    {
        m_labelWidth     = 120;
        m_labelsMeasured = 0;
    }
    else if (labels.size() != m_labelsMeasured)
    {
        // Labels are few (one per instruction of the program): measure them all.
        int w = 120;
        for (const QString &label : labels)
            w = std::max(w, fontMetrics().horizontalAdvance(label) + 16);
        m_labelWidth     = std::min(w, 260);
        m_labelsMeasured = labels.size();
    }
    updateScrollRange();
    followCurrentCycle();
    viewport()->update();
}

void TimingGridView::followCurrentCycle()
{
    horizontalScrollBar()->setValue(horizontalScrollBar()->maximum());
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void TimingGridView::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollRange();
}

void TimingGridView::clearSelection()
{
    m_selRow = m_selCol = -1;
    viewport()->update();
}

void TimingGridView::restoreSelection(int row, int column)
{
    if (row < 0 || column < 0 || row >= grid().rows.size() || column >= grid().columns())
        return;
    m_selRow = row;
    m_selCol = column;
    viewport()->update();
}

void TimingGridView::select(int row, int column)
{
    if (row < 0 || column < 0 || row >= grid().rows.size() || column >= grid().columns())
        return;
    m_selRow = row;
    m_selCol = column;
    viewport()->update();
    emit cellSelected(row, column);
}

void TimingGridView::mousePressEvent(QMouseEvent *event)
{
    const QPoint p  = event->position().toPoint();
    const int    lw = labelWidth();
    if (p.x() < lw || p.y() < kHeaderH)
        return;
    const int col = (p.x() - lw + horizontalScrollBar()->value()) / kCellW;
    const int row = (p.y() - kHeaderH + verticalScrollBar()->value()) / kCellH;
    select(row, col);
}

void TimingGridView::paintEvent(QPaintEvent *)
{
    QPainter              p(viewport());
    const TimingGridData &g     = grid();
    const ThemeManager   &theme = ThemeManager::getInstance();
    const QColor          base  = surface();
    const QColor          text  = theme.getTextColor();
    // Header numbers and flushed labels use the theme's text colour, not the
    // placeholder grey: that read at well under 4.5:1 on Tokyo Night. The
    // strike-through, not a fainter colour, marks a flushed row.
    const QColor muted = text;
    const QColor line  = blend(text, base, 0.15);
    const QRect  vp    = viewport()->rect();
    p.fillRect(vp, base);
    const int lw   = labelWidth();
    const int cols = g.columns();
    const int nrow = static_cast<int>(g.rows.size());
    const int sx   = horizontalScrollBar()->value();
    const int sy   = verticalScrollBar()->value();

    // Only what shows: columns c0..c1, rows r0..r1.
    const int c0 = std::min(cols, sx / kCellW);
    const int c1 = std::min(cols, (sx + std::max(0, vp.width() - lw)) / kCellW + 1);
    const int r0 = std::min(nrow, sy / kCellH);
    const int r1 = std::min(nrow, (sy + std::max(0, vp.height() - kHeaderH)) / kCellH + 1);
    auto colX = [&](int c) { return lw + c * kCellW - sx; };
    auto rowY = [&](int r) { return kHeaderH + r * kCellH - sy; };

    // The cells, clipped to the area right of the labels and under the header.
    p.save();
    p.setClipRect(QRect(lw, kHeaderH, vp.width() - lw, vp.height() - kHeaderH));
    // The current cycle (the last recorded one) is highlighted.
    if (cols > 0)
        p.fillRect(QRect(colX(cols - 1), 0, kCellW, vp.height()), theme.getCurrentLineColor());
    for (int r = r0; r < r1; ++r)
    {
        const TimingRow &row = g.rows[r];
        const int        y   = rowY(r);
        // Only the row's own cycles can hold anything.
        const int from = std::max(c0, static_cast<int>(row.firstCycle - g.firstCycle));
        const int to   = std::min(c1, static_cast<int>(row.lastCycle() - g.firstCycle) + 1);
        for (int c = from; c < to; ++c)
        {
            const TimingCell cell = row.at(g.firstCycle + static_cast<uint32_t>(c));
            if (cell.kind == TimingCell::Empty)
                continue;
            const QRect            box(colX(c) + 1, y + 1, kCellW - 2, kCellH - 2);
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
        p.setPen(line);
        p.drawLine(lw, y + kCellH, vp.width(), y + kCellH);
    }
    if (m_selRow >= r0 && m_selRow < r1 && m_selCol >= c0 && m_selCol < c1)
    {
        p.setPen(QPen(theme.getInfoColor(), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRect(colX(m_selCol), rowY(m_selRow), kCellW, kCellH));
    }
    p.restore();

    // Header: cycle numbers, pinned to the top.
    p.save();
    p.setClipRect(QRect(lw, 0, vp.width() - lw, kHeaderH));
    p.fillRect(QRect(lw, 0, vp.width() - lw, kHeaderH), base);
    if (cols > 0)
        p.fillRect(QRect(colX(cols - 1), 0, kCellW, kHeaderH), theme.getCurrentLineColor());
    p.setPen(muted);
    for (int c = c0; c < c1; ++c)
        p.drawText(QRect(colX(c), 0, kCellW, kHeaderH), Qt::AlignCenter,
                   QString::number(g.firstCycle + static_cast<uint32_t>(c)));
    p.restore();

    // Labels, pinned to the left.
    p.save();
    p.setClipRect(QRect(0, kHeaderH, lw, vp.height() - kHeaderH));
    p.fillRect(QRect(0, kHeaderH, lw, vp.height() - kHeaderH), base);
    for (int r = r0; r < r1; ++r)
    {
        const TimingRow &row = g.rows[r];
        const int        y   = rowY(r);
        p.setPen(row.squashed ? muted : text);
        QFont f = font();
        f.setStrikeOut(row.squashed); // a flushed instruction is struck through
        p.setFont(f);
        p.drawText(QRect(6, y, lw - 10, kCellH), Qt::AlignVCenter | Qt::AlignLeft,
                   fontMetrics().elidedText(row.label, Qt::ElideRight, lw - 10));
        p.setFont(font());
        p.setPen(line);
        p.drawLine(0, y + kCellH, lw, y + kCellH);
    }
    p.restore();
    p.fillRect(QRect(0, 0, lw, kHeaderH), base);
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

    m_view = new TimingGridView(this);
    m_view->setMinimumHeight(140);
    layout->addWidget(m_view, 1);

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

void TimingGridPanel::setUp(ProcessorManager *manager, QAction *stepAction, QAction *undoAction,
                            QAction *redoAction)
{
    m_manager    = manager;
    m_stepAction = stepAction;
    m_undoAction = undoAction;
    m_redoAction = redoAction;
    m_stepBack->setDefaultAction(undoAction); // step back IS Undo: the same action
    m_stepBack->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    if (manager)
        connect(manager, &ProcessorManager::processorStateChangedSignal, this, &TimingGridPanel::scheduleRefresh,
                Qt::QueuedConnection);
    auto sync = [this]()
    {
        const bool canMove = (m_stepAction && m_stepAction->isEnabled()) ||
                             (m_undoAction && m_undoAction->isEnabled()) ||
                             (m_redoAction && m_redoAction->isEnabled());
        m_jumpGo->setEnabled(canMove && !jumping());
    };
    if (stepAction)
        connect(stepAction, &QAction::changed, this, sync);
    if (undoAction)
        connect(undoAction, &QAction::changed, this, sync);
    if (redoAction)
        connect(redoAction, &QAction::changed, this, sync);
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

void TimingGridPanel::useTrace(const ExecutionTrace *trace)
{
    m_traceOverride = trace;
}

const ExecutionTrace *TimingGridPanel::trace() const
{
    if (m_traceOverride)
        return m_traceOverride;
    return m_manager ? &m_manager->trace() : nullptr;
}

void TimingGridPanel::scheduleRefresh()
{
    // A Run clocks many cycles per event-loop turn; one sync covers them all.
    if (m_refreshQueued)
        return;
    m_refreshQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_refreshQueued = false;
        refresh();
    });
}

void TimingGridPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_stale)
        refresh();
}

void TimingGridPanel::refresh()
{
    if (!m_manager)
    {
        m_message->setText("No processor.");
        return;
    }
    // Every Undo, Redo and Step asks for a refresh. During a jump that was one
    // grid update per cycle; the jump refreshes once when it lands instead.
    if (jumping())
        return;
    // Closed, the grid costs nothing: it catches up when it is opened.
    if (!isVisible())
    {
        m_stale = true;
        return;
    }
    m_stale = false;
    const TraceMeta meta = m_manager->traceMeta();
    if (meta.singleCycle)
    {
        m_message->setText("Single-cycle processor: every instruction runs from fetch to "
                           "write-back in one cycle, so there is no pipeline timing to draw. "
                           "Choose a 5-stage processor (Processor...) to see the grid.");
        m_message->show();
        m_view->hide();
        m_view->model().clear();
        m_view->gridChanged();
        clearSelection();
        return;
    }
    m_view->show();
    const ExecutionTrace &t = *trace();
    if (m_view->model().sync(t, m_manager->currentProgram()))
    {
        m_view->gridChanged(); // follows the current cycle
        keepOrClearSelection(meta);
    }
    const TimingGridData &g = m_view->grid();
    if (g.columns() == 0)
        m_message->setText("Step or run a program to draw its timing.");
    else if (g.firstCycle > 1)
    {
        const QLocale en(QLocale::English);
        m_message->setText(QStringLiteral("Showing cycles %1 to %2; older cycles were dropped (the trace keeps "
                                          "%3). Each row is one fetched instruction; * = stalled, hatched = "
                                          "bubble, crossed = flushed.")
                               .arg(en.toString(g.firstCycle), en.toString(g.lastCycle),
                                    en.toString(static_cast<qulonglong>(t.capacity()))));
    }
    else
        m_message->setText(QStringLiteral("Cycle %1 of the run. Each row is one fetched instruction; "
                                          "* = stalled, hatched = bubble, crossed = flushed.")
                               .arg(g.lastCycle));
}

void TimingGridPanel::jumpToCycle(uint32_t cycle)
{
    if (!m_manager || jumping())
        return;
    m_jumpTarget  = cycle;
    m_redoDrained  = false;
    m_jumpGo->setEnabled(false);
    QTimer::singleShot(0, this, &TimingGridPanel::jumpChunk);
}

void TimingGridPanel::jumpChunk()
{
    if (!m_jumpTarget || !m_manager)
        return;
    const uint32_t target = *m_jumpTarget;
    // About kJumpChunkMs of moves per event-loop turn: the window repaints
    // and stays responsive between chunks, however far the jump.
    QElapsedTimer chunk;
    chunk.start();
    while (chunk.elapsed() < kJumpChunkMs)
    {
        const uint32_t now = m_manager->getCycles();
        // Forward: Redo first, the same action as the Redo button, while the
        // machine has undone cycles to give back; Step for the rest. After a
        // Run to the end Step is disabled, so Step alone could not move.
        const bool useRedo = now < target && !m_redoDrained && m_redoAction && m_redoAction->isEnabled();
        QAction   *action  = now > target ? m_undoAction.data()
                             : now < target ? (useRedo ? m_redoAction.data() : m_stepAction.data())
                                            : nullptr;
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
            if (useRedo)
            {
                // Nothing left to redo: Step from here on.
                m_redoDrained = true;
                continue;
            }
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

void TimingGridPanel::clearSelection()
{
    m_selected.reset();
    m_view->clearSelection();
    m_explain->clear(); // back to the placeholder hint
}

void TimingGridPanel::keepOrClearSelection(const TraceMeta &meta)
{
    if (!m_selected)
        return;
    // The sentence stays only while the very cell it describes is drawn: the
    // same model, the same cycle with the same record, the same instance.
    // A new Run, a reload, a reset, a model switch or an Undo past that cycle
    // all fail one of these, and the old sentence would then be false.
    const TimingGridData &g     = m_view->grid();
    const uint32_t        cycle = m_selected->record.cycle;
    if (meta.model == m_selected->model && g.columns() > 0 && cycle >= g.firstCycle && cycle <= g.lastCycle)
    {
        const std::vector<TraceCycle> at = trace()->window(cycle, 1);
        const int                     row = g.rowOfId(m_selected->id);
        if (at.size() == 1 && at.front() == m_selected->record && row >= 0)
        {
            m_view->restoreSelection(row, static_cast<int>(cycle - g.firstCycle));
            return;
        }
    }
    clearSelection();
}

void TimingGridPanel::explainSelection(int row, int column)
{
    const TimingGridData &g = m_view->grid();
    if (!m_manager || row < 0 || column < 0 || row >= g.rows.size() || column >= g.columns())
        return;
    // The sentence comes from the trace's record for that cycle and nothing
    // else (trace_explain.h): no stall is mentioned that it did not record.
    // A stall's sentence looks at the cycles around it, so it gets a slice.
    constexpr uint32_t kContext = 32;
    const uint32_t     cycle    = g.firstCycle + static_cast<uint32_t>(column);
    const uint32_t     from     = std::max(g.firstCycle, cycle > kContext ? cycle - kContext : 1u);
    const uint32_t     to       = std::min(g.lastCycle, cycle + kContext);
    const std::vector<TraceCycle> records = trace()->window(from, to - from + 1);
    const size_t                  index   = cycle - from;
    if (index >= records.size() || records[index].cycle != cycle)
        return;
    const TraceMeta meta = m_manager->traceMeta();
    m_selected           = SelectedCell{meta.model, g.rows[row].id, records[index]};
    const AssembledProgram &program = m_manager->currentProgram();
    const std::string       text    = explainTraceCell(
        records, index, g.rows[row].id, ExplainModel{meta.hazardDetection, meta.forwarding},
        [&program](uint64_t pc) { return traceDisassembly(program, pc).toStdString(); });
    m_explain->setPlainText(QString::fromStdString(text));
}
} // namespace Atlas
