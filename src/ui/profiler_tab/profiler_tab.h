#ifndef PROFILERTAB_H
#define PROFILERTAB_H

#include "ui/common/atlas_tab.h"
#include <QString>
#include <QWidget>
#include <map>
#include <string>



namespace Atlas
{
  
class Profiler; // Forward declaration to avoid including profiler header
class ProcessorManager;
class ProcessorMetricsModel;
class MoreMetricsPanel;

namespace Ui
{
class ProfilerTab;
}

class ProfilerTab : public AtlasTab
{
    Q_OBJECT

public:
    explicit ProfilerTab(QWidget *parent = nullptr, const Profiler *profiler = nullptr);
    ~ProfilerTab();

    /// Adds the run-counter table (cycles, retired, CPI, IPC, stalls,
    /// mispredictions) above the instruction-type table, fed from the
    /// manager's metrics() and refreshed on every state change.
    void setProcessorManager(ProcessorManager *manager);

    /// The "More metrics" group below the two tables (nullptr until
    /// setProcessorManager).
    MoreMetricsPanel *moreMetrics() const { return m_moreMetrics; }

public slots:
    void setSourceText(const QString &sourceText);
    void updateLineExecutionCount(const std::map<int, int> &lineExecutionCounts);
    void updateLineInstructionType(const std::map<int, std::string> &lineInstructionTypes);
    void updateStatistics(const std::map<std::string, int> &statistics);
    void resetProfilerView();

private:
    Ui::ProfilerTab *ui;
    const Profiler *m_profiler = nullptr;
    ProcessorMetricsModel *m_metricsModel = nullptr;
    MoreMetricsPanel *m_moreMetrics = nullptr;
};
} // namespace Atlas
#endif // PROFILERTAB_H
