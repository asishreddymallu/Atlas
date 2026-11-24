#include "ui/profiler_tab/more_metrics_panel.h"

#include "processor/processor_manager.h"
#include "profiler_metrics/branch_stats.h"
#include "ui/common/data_table_style.h"
#include "ui/common/processor_metrics_model.h"

#include <QHeaderView>
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
    m_types->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Retired ") + kMeasured,
                                        QStringLiteral("Share ") + kModelled});
    m_mnemonics = new QStandardItemModel(0, 3, this);
    m_mnemonics->setHorizontalHeaderLabels({QStringLiteral("Mnemonic"),
                                            QStringLiteral("Retired ") + kMeasured,
                                            QStringLiteral("Share ") + kModelled});

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName(QStringLiteral("moreMetricsTabs"));
    m_tabs->addTab(makeTable(m_summary, QStringLiteral("moreMetricsSummaryView"), m_tabs),
                   QStringLiteral("Summary"));
    m_tabs->addTab(makeTable(m_types, QStringLiteral("moreMetricsTypeView"), m_tabs),
                   QStringLiteral("By type"));
    m_tabs->addTab(makeTable(m_mnemonics, QStringLiteral("moreMetricsMnemonicView"), m_tabs),
                   QStringLiteral("By mnemonic"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addWidget(m_tabs);

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
    const QString key = QStringLiteral("%1/%2/%3/%4/%5/%6")
                            .arg(s.lastCycle)
                            .arg(s.mix.retired)
                            .arg(static_cast<int>(s.processor))
                            .arg(s.programEnded)
                            .arg(s.complete)
                            .arg(s.cycles);
    if (key == m_lastKey && m_summary->rowCount() > 0)
        return;
    m_lastKey = key;
    const InstructionMix &mix = s.mix;
    const QString incomplete = s.complete ? QString()
                                          : QStringLiteral(" Some cycles were not seen, so the "
                                                           "totals are incomplete.");

    m_summary->removeRows(0, m_summary->rowCount());
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

    m_types->removeRows(0, m_types->rowCount());
    for (size_t t = 0; t < mix.byType.size(); ++t)
    {
        const auto type = static_cast<instruction_set::InstructionType>(t);
        m_types->appendRow({cell(QString::fromStdString(instruction_set::instructionTypeName(type)), false),
                            cell(QString::number(mix.byType[t])),
                            cell(percent(shareOf(mix.byType[t], mix.retired)))});
    }

    m_mnemonics->removeRows(0, m_mnemonics->rowCount());
    for (const MnemonicCount &m : mix.byMnemonic)
        m_mnemonics->appendRow({cell(QString::fromStdString(m.mnemonic), false),
                                cell(QString::number(m.count)),
                                cell(percent(shareOf(m.count, mix.retired)))});
}
} // namespace Atlas
