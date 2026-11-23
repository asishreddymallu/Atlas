#include "vm_state_table_model.h"
#include "ui/common/processor_metrics_model.h"
#include "ui/common/table_font.h"

namespace Atlas
{
VMStateTableModel::VMStateTableModel(QObject *parent, ProcessorManager *vmManager)
    : QAbstractTableModel(parent), m_vmManager(vmManager)
{

}

int VMStateTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 7;
}

int VMStateTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 2; // Key and Value
}

QVariant VMStateTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal)
    {
        if (section == 0)
            return QString("Key");
        else if (section == 1)
            return QString("Value");
    }
    return QVariant{};
}
QVariant VMStateTableModel::data(const QModelIndex &index, int role) const
{
    // Define the keys in the order they should appear
    // cuz we dont want to display all the keys in vm state map
    static const QStringList keys = {
        "ProgramCounter",      "Cycles", "InstructionsRetired", "CPI", "IPC", "StallCycles",
        "BranchMispredictions"};
    enum class VMStateKey
    {
        ProgramCounter,
        Cycles,
        InstructionsRetired,
        CPI,
        IPC,
        StallCycles,
        BranchMispredictions
    };
  
    if (!m_vmManager)
        return QVariant{};

    if (role == Qt::TextAlignmentRole)
    {
        return index.column() == 1
                   ? QVariant(int(Qt::AlignRight | Qt::AlignVCenter))
                   : QVariant(int(Qt::AlignLeft | Qt::AlignVCenter));
    }

    if (role == Qt::FontRole)
    {
        return tableNumericFont();
    }

    // Rows 1..6 are the run counters, read from the same snapshot and
    // through the same helpers as the Profiler's table.
    const auto metricRow = [](int row) { return static_cast<MetricRow>(row - 1); };
    const bool isMetric  = index.row() >= 1 && index.row() <= static_cast<int>(MetricRow::Count);

    if (role == Qt::ToolTipRole && isMetric)
        return metricToolTip(m_vmManager->metrics(), metricRow(index.row()));

    if (role == Qt::DisplayRole)
    {
        if (index.column() == 0)
            return keys[index.row()];
        if (index.column() == 1)
        {
            if (static_cast<VMStateKey>(index.row()) == VMStateKey::ProgramCounter)
                // QVariant has no uint64_t overload and would take a double
                return QVariant::fromValue(
                    static_cast<qulonglong>(m_vmManager->getProgramCounter()));
            if (isMetric)
                return metricValue(m_vmManager->metrics(), metricRow(index.row()));
        }
    }
    return QVariant{};
}

void VMStateTableModel::vmStateChangedSlot(const QMap<QString, QVariant> &processorState)
{
    Q_UNUSED(processorState);
    refresh();
}

void VMStateTableModel::refresh()
{
    beginResetModel();
    endResetModel();
}
} // namespace Atlas
