/**
 * @file more_metrics_panel.h
 * @brief The Profiler's "More metrics" group, below the existing tables.
 *
 * Every number carries [Measured] (counted by the simulator) or [Modelled]
 * (computed from other numbers), in the same words as the counter table,
 * and reads "n/a" when the model cannot supply it or a denominator is 0.
 * Read-only: it shows what MetricsCollector gathered.
 */
#pragma once

#include "ui/profiler_tab/metrics_collector.h"

#include <QGroupBox>

#include <optional>

class QStandardItemModel;
class QTabWidget;
class QTimer;

namespace Atlas
{
class ProcessorManager;

class MoreMetricsPanel : public QGroupBox
{
    Q_OBJECT
  public:
    MoreMetricsPanel(ProcessorManager *manager, QWidget *parent = nullptr);

    MetricsCollector   *collector() const { return m_collector; }
    QTabWidget         *tabs() const { return m_tabs; }
    QStandardItemModel *summaryModel() const { return m_summary; }
    QStandardItemModel *typeModel() const { return m_types; }
    QStandardItemModel *mnemonicModel() const { return m_mnemonics; }

    /// "37.50 %", or "n/a".
    static QString percent(std::optional<double> fraction);
    /// The value in a summary row, by its label ("" if there is no such row).
    QString summaryValue(const QString &label) const;
    QString summarySource(const QString &label) const;

  public slots:
    /// Re-reads the collector and redraws every table (if anything changed).
    void refresh();

  private:
    void addSummaryRow(const QString &label, const QString &value, bool measured, const QString &tip);

    ProcessorManager   *m_manager{nullptr};
    MetricsCollector   *m_collector{nullptr};
    QTabWidget         *m_tabs{nullptr};
    QStandardItemModel *m_summary{nullptr};
    QStandardItemModel *m_types{nullptr};
    QStandardItemModel *m_mnemonics{nullptr};
    QTimer             *m_timer{nullptr};
    QString             m_lastKey; ///< what the tables last showed, to skip redundant redraws
};
} // namespace Atlas
