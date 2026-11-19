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

ProcessorMetricsModel::ProcessorMetricsModel(QObject *parent, ProcessorManager *manager)
    : QAbstractTableModel(parent), m_manager(manager)
{
    if (m_manager)
        m_metrics = m_manager->metrics();
}

int ProcessorMetricsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(MetricRow::Count);
}

int ProcessorMetricsModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant ProcessorMetricsModel::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};
    return section == 0 ? QStringLiteral("Counter") : QStringLiteral("Value");
}

QVariant ProcessorMetricsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(MetricRow::Count))
        return {};
    const auto row = static_cast<MetricRow>(index.row());
    switch (role)
    {
    case Qt::DisplayRole:
        return index.column() == 0 ? QVariant(metricLabel(row)) : metricValue(m_metrics, row);
    case Qt::ToolTipRole: return metricToolTip(m_metrics, row);
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
