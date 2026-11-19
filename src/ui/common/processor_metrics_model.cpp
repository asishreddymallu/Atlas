#include "processor_metrics_model.h"
#include "ui/common/table_font.h"

namespace Kites
{
QString metricLabel(MetricRow row)
{
    switch (row)
    {
    case MetricRow::Cycles: return QStringLiteral("Cycles");
    case MetricRow::Retired: return QStringLiteral("Instructions retired");
    case MetricRow::CPI: return QStringLiteral("CPI");
    case MetricRow::IPC: return QStringLiteral("IPC");
    case MetricRow::Stalls: return QStringLiteral("Stall cycles");
    case MetricRow::Mispredictions: return QStringLiteral("Branch mispredictions");
    case MetricRow::Count: break;
    }
    return {};
}

QVariant metricValue(const ProcessorMetrics &m, MetricRow row)
{
    const QString notApplicable = QStringLiteral("n/a");
    switch (row)
    {
    case MetricRow::Cycles: return m.cycles;
    case MetricRow::Retired: return m.retired;
    // Undefined until something has retired (CPI) or a cycle has run
    // (IPC): a dash, not a 0.00 that reads like a measurement.
    case MetricRow::CPI: return m.retired ? QString::number(m.cpi, 'f', 2) : QStringLiteral("-");
    case MetricRow::IPC: return m.cycles ? QString::number(m.ipc, 'f', 2) : QStringLiteral("-");
    case MetricRow::Stalls: return m.stalls ? QVariant(*m.stalls) : QVariant(notApplicable);
    case MetricRow::Mispredictions:
        return m.mispredictions ? QVariant(*m.mispredictions) : QVariant(notApplicable);
    case MetricRow::Count: break;
    }
    return {};
}

QString metricToolTip(const ProcessorMetrics &m, MetricRow row)
{
    switch (row)
    {
    case MetricRow::Cycles: return QStringLiteral("Clock cycles since the program was loaded");
    case MetricRow::Retired:
        return QStringLiteral("Instructions that completed (reached write-back)");
    case MetricRow::CPI: return QStringLiteral("Cycles per retired instruction");
    case MetricRow::IPC: return QStringLiteral("Retired instructions per cycle");
    case MetricRow::Stalls: return QString::fromUtf8(m.stallsNote);
    case MetricRow::Mispredictions: return QString::fromUtf8(m.mispredictionsNote);
    case MetricRow::Count: break;
    }
    return {};
}

namespace
{
QVariant optionalValue(const std::optional<unsigned int> &v)
{
    return v ? QVariant(*v) : QVariant(QStringLiteral("n/a"));
}
} // namespace

QString profilerRowLabel(ProfilerRow row)
{
    switch (row)
    {
    case ProfilerRow::Cycles: return metricLabel(MetricRow::Cycles);
    case ProfilerRow::Retired: return metricLabel(MetricRow::Retired);
    case ProfilerRow::CPI: return metricLabel(MetricRow::CPI);
    case ProfilerRow::IPC: return metricLabel(MetricRow::IPC);
    case ProfilerRow::Stalls: return metricLabel(MetricRow::Stalls);
    case ProfilerRow::StallsLoadUse: return QStringLiteral("    load-use");
    case ProfilerRow::StallsRawNoForwarding: return QStringLiteral("    RAW, no forwarding");
    case ProfilerRow::Flushed: return QStringLiteral("Flushed cycles");
    case ProfilerRow::FlushedBranch: return QStringLiteral("    taken branches");
    case ProfilerRow::FlushedJump: return QStringLiteral("    jumps");
    case ProfilerRow::FlushedTrap: return QStringLiteral("    CSR and ecall");
    case ProfilerRow::Mispredictions: return metricLabel(MetricRow::Mispredictions);
    case ProfilerRow::Count: break;
    }
    return {};
}

QVariant profilerRowValue(const ProcessorMetrics &m, ProfilerRow row)
{
    switch (row)
    {
    case ProfilerRow::Cycles: return metricValue(m, MetricRow::Cycles);
    case ProfilerRow::Retired: return metricValue(m, MetricRow::Retired);
    case ProfilerRow::CPI: return metricValue(m, MetricRow::CPI);
    case ProfilerRow::IPC: return metricValue(m, MetricRow::IPC);
    case ProfilerRow::Stalls: return metricValue(m, MetricRow::Stalls);
    case ProfilerRow::StallsLoadUse: return optionalValue(m.stallsLoadUse);
    case ProfilerRow::StallsRawNoForwarding: return optionalValue(m.stallsRawNoForwarding);
    case ProfilerRow::Flushed: return optionalValue(m.flushed);
    case ProfilerRow::FlushedBranch: return optionalValue(m.flushedBranch);
    case ProfilerRow::FlushedJump: return optionalValue(m.flushedJump);
    case ProfilerRow::FlushedTrap: return optionalValue(m.flushedTrap);
    case ProfilerRow::Mispredictions: return metricValue(m, MetricRow::Mispredictions);
    case ProfilerRow::Count: break;
    }
    return {};
}

QString profilerRowToolTip(const ProcessorMetrics &m, ProfilerRow row)
{
    QString what;
    switch (row)
    {
    case ProfilerRow::Cycles: what = metricToolTip(m, MetricRow::Cycles); break;
    case ProfilerRow::Retired: what = metricToolTip(m, MetricRow::Retired); break;
    case ProfilerRow::CPI: what = metricToolTip(m, MetricRow::CPI); break;
    case ProfilerRow::IPC: what = metricToolTip(m, MetricRow::IPC); break;
    case ProfilerRow::Stalls: what = metricToolTip(m, MetricRow::Stalls); break;
    case ProfilerRow::StallsLoadUse:
        what = m.stallsLoadUse ? QStringLiteral("Stall cycles behind a load whose value a reader needs at once "
                                                "(forwarding cannot help). From the pipeline trace.")
                               : QString::fromUtf8(m.stallsNote);
        break;
    case ProfilerRow::StallsRawNoForwarding:
        what = m.stallsRawNoForwarding
                   ? QStringLiteral("Stall cycles waiting for a producer to write back, with no forwarding. "
                                    "From the pipeline trace.")
                   : QString::fromUtf8(m.stallsNote);
        break;
    case ProfilerRow::Flushed:
        what = m.flushed ? QStringLiteral("Pipeline slots emptied by redirects: 3 per taken branch (resolved in "
                                          "MEM), 2 per jump (EX), 4 per CSR instruction or ecall (WB). Counted "
                                          "from the pipeline trace.")
                         : QStringLiteral("Single-cycle: nothing is in flight, so nothing is flushed.");
        break;
    case ProfilerRow::FlushedBranch:
    case ProfilerRow::FlushedJump:
    case ProfilerRow::FlushedTrap:
        what = m.flushed ? QStringLiteral("Part of Flushed cycles. From the pipeline trace.")
                         : QStringLiteral("Single-cycle: nothing is in flight, so nothing is flushed.");
        break;
    case ProfilerRow::Mispredictions: what = metricToolTip(m, MetricRow::Mispredictions); break;
    case ProfilerRow::Count: break;
    }
    return provenanceTag(profilerRowProvenance(row)) + " " + what;
}

Provenance profilerRowProvenance(ProfilerRow row)
{
    // CPI and IPC are ratios of two counted numbers; everything else is
    // counted by the simulator as it runs.
    return row == ProfilerRow::CPI || row == ProfilerRow::IPC ? Provenance::Modelled : Provenance::Measured;
}

QString provenanceTag(Provenance provenance)
{
    return provenance == Provenance::Measured ? QStringLiteral("[Measured]") : QStringLiteral("[Modelled]");
}

QString provenanceToolTip(Provenance provenance)
{
    return provenance == Provenance::Measured
               ? QStringLiteral("Counted by the simulator, cycle by cycle.")
               : QStringLiteral("Derived from counted numbers by a formula (CPI = cycles / retired, IPC = "
                                "retired / cycles).");
}

ProcessorMetricsModel::ProcessorMetricsModel(QObject *parent, ProcessorManager *manager)
    : QAbstractTableModel(parent), m_manager(manager)
{
    if (m_manager)
        m_metrics = m_manager->metrics();
}

int ProcessorMetricsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(ProfilerRow::Count);
}

int ProcessorMetricsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 3;
}

QVariant ProcessorMetricsModel::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    switch (section)
    {
    case 0: return QStringLiteral("Counter");
    case 1: return QStringLiteral("Value");
    default: return QStringLiteral("Source");
    }
}

QVariant ProcessorMetricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(ProfilerRow::Count))
        return {};
    const auto       row        = static_cast<ProfilerRow>(index.row());
    const Provenance provenance = profilerRowProvenance(row);
    switch (role)
    {
    case Qt::DisplayRole:
        switch (index.column())
        {
        case 0: return profilerRowLabel(row);
        case 1: return profilerRowValue(m_metrics, row);
        default: return provenanceTag(provenance);
        }
    case Qt::ToolTipRole:
        return index.column() == 2 ? provenanceToolTip(provenance) : profilerRowToolTip(m_metrics, row);
    case Qt::TextAlignmentRole:
        return index.column() == 1 ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                                   : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    case Qt::FontRole: return tableNumericFont();
    default: return {};
    }
}

void ProcessorMetricsModel::refresh()
{
    if (!m_manager)
        return;
    beginResetModel();
    m_metrics = m_manager->metrics();
    endResetModel();
}
} // namespace Kites
