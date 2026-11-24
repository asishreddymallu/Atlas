#include "ui/profiler_tab/more_metrics_panel.h"

#include "processor/processor_manager.h"
#include "profiler_metrics/branch_stats.h"
#include "profiler_metrics/speedup.h"
#include "ui/profiler_tab/rvss_replay.h"
#include "ui/common/data_table_style.h"
#include "ui/common/processor_metrics_model.h"

#include <QHeaderView>
#include <QLabel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace Atlas
{
namespace
{
QTableView *makeTable(QStandardItemModel *model, const QString &name, QWidget *parent)
{
    auto *view = new QTableView(parent);
    view->setObjectName(name);
    view->setModel(model);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setSelectionMode(QAbstractItemView::NoSelection);
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    view->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    applyDataTableStyle(view);
    return view;
}

QStandardItem *cell(const QString &text, bool number = true)
{
    auto *item = new QStandardItem(text);
    item->setEditable(false);
    if (number)
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return item;
}

const QString kMeasured = provenanceTag(Provenance::Measured);
const QString kModelled = provenanceTag(Provenance::Modelled);
} // namespace

MoreMetricsPanel::MoreMetricsPanel(ProcessorManager *manager, QWidget *parent)
    : QGroupBox(QStringLiteral("More metrics"), parent), m_manager(manager)
{
    setObjectName(QStringLiteral("moreMetricsGroup"));
    m_collector = new MetricsCollector(manager, this);

    m_summary   = new QStandardItemModel(0, 3, this);
    m_summary->setHorizontalHeaderLabels({QStringLiteral("Metric"), QStringLiteral("Value"),
                                          QStringLiteral("Source")});
    m_types = new QStandardItemModel(0, 3, this);
    // The tag goes on a second header line, so every column fits the
    // Profiler's narrow pane.
    m_types->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Retired\n") + kMeasured,
                                        QStringLiteral("Share\n") + kModelled});
    m_mnemonics = new QStandardItemModel(0, 3, this);
    m_mnemonics->setHorizontalHeaderLabels({QStringLiteral("Mnemonic"),
                                            QStringLiteral("Retired\n") + kMeasured,
                                            QStringLiteral("Share\n") + kModelled});

    m_loops = new QStandardItemModel(0, 4, this);
    // Full names now the pane is wide enough (known_issues 147); the tag on
    // a second line keeps every column narrow.
    m_loops->setHorizontalHeaderLabels({QStringLiteral("Lines"), QStringLiteral("Iterations\n") + kMeasured,
                                        QStringLiteral("Instructions executed\n") + kMeasured,
                                        QStringLiteral("Share\n") + kModelled});
    m_loops->setHeaderData(0, Qt::Horizontal, QStringLiteral("The loop's source lines: target to branch."),
                           Qt::ToolTipRole);
    m_loops->setHeaderData(1, Qt::Horizontal, QStringLiteral("Iterations: times the loop's first instruction executed."),
                           Qt::ToolTipRole);
    m_loops->setHeaderData(2, Qt::Horizontal,
                           QStringLiteral("Instructions executed in the loop's lines (by instructions, not cycles)."),
                           Qt::ToolTipRole);
    m_loops->setHeaderData(3, Qt::Horizontal, QStringLiteral("Instructions executed in the loop / instructions retired."),
                           Qt::ToolTipRole);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("moreMetricsTabs"));
    m_tabs->addTab(makeTable(m_summary, QStringLiteral("moreMetricsSummaryView"), m_tabs),
                   QStringLiteral("Summary"));
    m_tabs->addTab(makeTable(m_types, QStringLiteral("moreMetricsTypeView"), m_tabs),
                   QStringLiteral("By type"));
    m_tabs->addTab(makeTable(m_mnemonics, QStringLiteral("moreMetricsMnemonicView"), m_tabs),
                   QStringLiteral("By mnemonic"));
    QTableView *loopView = makeTable(m_loops, QStringLiteral("moreMetricsLoopView"), m_tabs);
    // Four columns in a narrow pane: every column sized to its contents and
    // the last one takes what is left, rather than the first stretching.
    loopView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    loopView->horizontalHeader()->setStretchLastSection(true);
    loopView->horizontalHeader()->setMinimumSectionSize(20);
    m_tabs->addTab(loopView, QStringLiteral("Hottest loops"));

    m_notes = new QLabel(this);
    m_notes->setObjectName(QStringLiteral("moreMetricsNotes"));
    m_notes->setWordWrap(true);
    m_notes->setProperty("paneTitle", true); // the theme's small muted caption
    m_notes->setText(QStringLiteral("Speed-up: ") + QString::fromLatin1(kSpeedupCaveat) +
                     QStringLiteral(". Hottest loops are ranked by instructions executed, not cycles: "
                                    "Atlas keeps no per-line cycle counts."));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tabs);
    layout->addWidget(m_notes);

    m_replay = new RvssReplay(this);
    connect(m_replay, &RvssReplay::finishedSignal, this,
            [this]
            {
                m_lastKey.clear(); // the speed-up can now be shown
                refresh();
            });

    // Redrawn at most five times a second during a run, and at once when a
    // run finishes or the state changes on the UI thread.
    m_timer = new QTimer(this);
    m_timer->setInterval(200);
    connect(m_timer, &QTimer::timeout, this, &MoreMetricsPanel::refresh);
    m_timer->start();
    connect(manager, &ProcessorManager::runFinishedSignal, this, &MoreMetricsPanel::refresh,
            Qt::QueuedConnection);
    refresh();
}

QString MoreMetricsPanel::percent(std::optional<double> fraction)
{
    if (!fraction)
        return QStringLiteral("n/a");
    return QString::number(*fraction * 100.0, 'f', 2) + QStringLiteral(" %");
}

void MoreMetricsPanel::addSummaryRow(const QString &label, const QString &value, bool measured,
                                     const QString &tip)
{
    QList<QStandardItem *> row{cell(label, false), cell(value), cell(measured ? kMeasured : kModelled)};
    for (QStandardItem *item : row)
        item->setToolTip(tip);
    row[2]->setToolTip(provenanceToolTip(measured ? Provenance::Measured : Provenance::Modelled));
    m_summary->appendRow(row);
}

QString MoreMetricsPanel::summaryValue(const QString &label) const
{
    for (int r = 0; r < m_summary->rowCount(); ++r)
        if (m_summary->item(r, 0)->text() == label)
            return m_summary->item(r, 1)->text();
    return QString();
}

QString MoreMetricsPanel::summarySource(const QString &label) const
{
    for (int r = 0; r < m_summary->rowCount(); ++r)
        if (m_summary->item(r, 0)->text() == label)
            return m_summary->item(r, 2)->text();
    return QString();
}

void MoreMetricsPanel::refresh()
{
    const MetricsCollector::Snapshot s = m_collector->snapshot();
    const QString key = QStringLiteral("%1/%2/%3/%4/%5/%6/%7")
                            .arg(s.programCounter)
                            .arg(s.lastCycle)
                            .arg(s.mix.retired)
                            .arg(static_cast<int>(s.processor))
                            .arg(s.programEnded)
                            .arg(s.complete)
                            .arg(s.cycles);
    // The single-cycle replay starts once per program, when it has ended.
    if (s.generation != m_replayGeneration)
    {
        m_replay->cancel();
        m_replayGeneration = UINT64_MAX;
    }
    if (s.programEnded && s.processor != ProcessorType::RVSS && m_replayGeneration == UINT64_MAX)
    {
        m_replayGeneration = s.generation;
        m_replay->start(m_manager->currentProgram());
    }
    if (key == m_lastKey && m_summary->rowCount() > 0)
        return;
    m_lastKey = key;
    const InstructionMix &mix = s.mix;
    const QString incomplete = s.complete ? QString()
                                          : QStringLiteral(" Some cycles were not seen, so the "
                                                           "totals are incomplete.");

    m_summary->removeRows(0, m_summary->rowCount());
    // From the Processor tab, which no longer shows counters (final/polish).
    addSummaryRow(QStringLiteral("Program counter"),
                  QStringLiteral("0x%1").arg(s.programCounter, 8, 16, QLatin1Char('0')), true,
                  QStringLiteral("The processor's program counter: the address of the next "
                                 "instruction to fetch."));
    addSummaryRow(QStringLiteral("Instructions retired (mix total)"), QString::number(mix.retired), true,
                  QStringLiteral("Machine instructions retired so far, counted from the execution "
                                 "trace. A pseudo-instruction counts as the instructions it "
                                 "expands to.") + incomplete);
    addSummaryRow(QStringLiteral("Loads retired"), QString::number(mix.loads), true,
                  QStringLiteral("lb, lbu, lh, lhu, lw, lwu, ld, flw and fld retired."));
    addSummaryRow(QStringLiteral("Loads share"), percent(shareOf(mix.loads, mix.retired)), false,
                  QStringLiteral("Loads retired / instructions retired."));
    addSummaryRow(QStringLiteral("Stores retired"), QString::number(mix.stores), true,
                  QStringLiteral("sb, sh, sw, sd, fsw and fsd retired."));
    addSummaryRow(QStringLiteral("Stores share"), percent(shareOf(mix.stores, mix.retired)), false,
                  QStringLiteral("Stores retired / instructions retired."));

    const std::optional<uint64_t> mispredictions =
        s.mispredictions ? std::optional<uint64_t>(*s.mispredictions) : std::nullopt;
    const BranchStats branches = computeBranchStats(s.branches, s.taken, mispredictions);
    addSummaryRow(QStringLiteral("Branches retired"), QString::number(branches.branches), true,
                  QStringLiteral("B-type instructions retired (beq, bne, blt, bge, bltu, bgeu)."));
    addSummaryRow(QStringLiteral("Taken branches"), QString::number(branches.taken), true,
                  QStringLiteral("Retired branches after which the next instruction to retire was "
                                 "not the one at branch address + 4."));
    addSummaryRow(QStringLiteral("Taken rate"), percent(branches.takenRate), false,
                  QStringLiteral("Taken branches / branches retired."));
    addSummaryRow(QStringLiteral("Misprediction rate"), percent(branches.mispredictionRate), false,
                  s.mispredictions
                      ? QStringLiteral("Branch mispredictions (the counter above) / branches retired.")
                      : QStringLiteral("n/a: this model keeps no misprediction counter (the "
                                       "single-cycle processor predicts nothing)."));

    // Speed-up: only for a program that has ended; the single-cycle cycles
    // come from a headless replay (or are this run's own on rvss).
    std::optional<unsigned> rvssCycles;
    QString                 why;
    if (!s.programEnded)
        why = QStringLiteral("n/a until the program has run to its end.");
    else if (s.processor == ProcessorType::RVSS)
        rvssCycles = s.cycles;
    else
        switch (m_replay->state())
        {
        case RvssReplay::State::Finished: rvssCycles = m_replay->cycles(); break;
        case RvssReplay::State::NeedsInput:
            why = QStringLiteral("n/a: the program needs input, so it cannot be replayed headless.");
            break;
        case RvssReplay::State::TooLong:
            why = QStringLiteral("n/a: the single-cycle replay did not end within %1 cycles.")
                      .arg(RvssReplay::kCycleCap);
            break;
        default: why = QStringLiteral("n/a while the single-cycle replay runs."); break;
        }
    const std::optional<double> speedup =
        speedupOver(rvssCycles, s.programEnded ? std::optional<unsigned>(s.cycles) : std::nullopt);
    addSummaryRow(QStringLiteral("Speed-up over single-cycle"),
                  speedup ? QString::number(*speedup, 'f', 2) + QStringLiteral("x") : QStringLiteral("n/a"),
                  false,
                  QStringLiteral("The program's cycles on the single-cycle processor / its cycles here") +
                      (rvssCycles ? QStringLiteral(" (%1 / %2).").arg(*rvssCycles).arg(s.cycles)
                                  : QStringLiteral(". ") + why) +
                      QStringLiteral(" Note: ") + QString::fromLatin1(kSpeedupCaveat) + QStringLiteral("."));

    // Forwarding: only the two models with a forwarding unit.
    const bool forwarding = s.processor == ProcessorType::RV5Stage_NH_F ||
                            s.processor == ProcessorType::RV5Stage_H_F;
    const QString noForwarding =
        QStringLiteral("n/a: this processor has no forwarding unit, so every operand comes from the "
                       "register file.");
    const auto fwd = [&](uint64_t n) { return forwarding ? QString::number(n) : QStringLiteral("n/a"); };
    addSummaryRow(QStringLiteral("Forwarded operands"), fwd(s.forwardsExMem + s.forwardsMemWb), true,
                  forwarding ? QStringLiteral("Operand values the instruction in EX took from a later "
                                              "pipeline register instead of the register file, counted "
                                              "for the whole run.") + incomplete
                             : noForwarding);
    addSummaryRow(QStringLiteral("Forwarded from EX/MEM"), fwd(s.forwardsExMem), true,
                  forwarding ? QStringLiteral("The value was the result of the instruction one ahead, "
                                              "in the EX/MEM register.")
                             : noForwarding);
    addSummaryRow(QStringLiteral("Forwarded from MEM/WB"), fwd(s.forwardsMemWb), true,
                  forwarding ? QStringLiteral("The value was in the MEM/WB register: an instruction two "
                                              "ahead, or a load one ahead after its stall.")
                             : noForwarding);

    m_types->removeRows(0, m_types->rowCount());
    for (size_t t = 0; t < mix.byType.size(); ++t)
    {
        const auto type = static_cast<instruction_set::InstructionType>(t);
        m_types->appendRow({cell(QString::fromStdString(instruction_set::instructionTypeName(type)), false),
                            cell(QString::number(mix.byType[t])),
                            cell(percent(shareOf(mix.byType[t], mix.retired)))});
    }

    m_loops->removeRows(0, m_loops->rowCount());
    for (const HotLoop &loop : s.hotLoops)
    {
        QList<QStandardItem *> row{
            cell(loop.firstLine == loop.lastLine ? QString::number(loop.firstLine)
                                                 : QStringLiteral("%1-%2").arg(loop.firstLine).arg(loop.lastLine),
                 false),
            cell(QString::number(loop.iterations)), cell(QString::number(loop.dynamic)),
            cell(percent(loop.share))};
        const QString tip =
            QStringLiteral("A backward branch or jump at line %1 to line %2. Iterations: times the "
                           "first instruction of line %2 executed. Instructions executed: the per-line "
                           "counts summed over lines %2-%1. Ranked by instructions executed, not cycles.")
                .arg(loop.lastLine)
                .arg(loop.firstLine);
        for (QStandardItem *item : row)
            item->setToolTip(tip);
        m_loops->appendRow(row);
    }

    m_mnemonics->removeRows(0, m_mnemonics->rowCount());
    for (const MnemonicCount &m : mix.byMnemonic)
        m_mnemonics->appendRow({cell(QString::fromStdString(m.mnemonic), false),
                                cell(QString::number(m.count)),
                                cell(percent(shareOf(m.count, mix.retired)))});
}
} // namespace Atlas
