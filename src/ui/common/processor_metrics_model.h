#pragma once
#include "processor/processor_manager.h"

#include <QAbstractTableModel>
#include <QString>
#include <QVariant>

namespace Kites
{
/// The run counters, one row each, in the order both tables show them.
enum class MetricRow
{
    Cycles = 0,
    Retired,
    CPI,
    IPC,
    Stalls,
    Mispredictions,
    Count
};

/// The text, value and tooltip of one counter. The Processor tab's state
/// table and the Profiler's table both go through these, from one
/// ProcessorManager::metrics() snapshot, so they always read the same.
/// A counter the model does not have reads "n/a", and its tooltip says why.
QString  metricLabel(MetricRow row);
QVariant metricValue(const ProcessorMetrics &metrics, MetricRow row);
QString  metricToolTip(const ProcessorMetrics &metrics, MetricRow row);

/// The Profiler's counter table.
class ProcessorMetricsModel : public QAbstractTableModel
{
    Q_OBJECT
  public:
    explicit ProcessorMetricsModel(QObject *parent = nullptr, ProcessorManager *manager = nullptr);

    int      rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int      columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

  public slots:
    /// Re-reads the counters. Connected to processorStateChangedSignal,
    /// which fires on every clock, Undo, Redo, load and switch.
    void refresh();

  private:
    ProcessorManager *m_manager{nullptr};
    ProcessorMetrics  m_metrics{};
};
} // namespace Kites
