/**
 * @file processor_tab_metrics_moved_test.cpp
 * @brief final/polish: the Processor tab is the diagram alone, and every
 *        value its old table showed (PC, cycles, retired, CPI, IPC, stall
 *        cycles, mispredictions) is in the Profiler tab with its label.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "ui/common/processor_metrics_model.h"
#include "ui/main_window/main_window.h"
#include "ui/processor_tab/circuit_view.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/timing_grid/timing_grid.h"
#include "utils/utils.h"

#include <QApplication>
#include <QTableView>

#include <gtest/gtest.h>

using namespace Atlas;

TEST(ProcessorTabMetricsMoved, TheProcessorTabIsTheDiagramAlone)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    window.setCurrentTab(MainWindow::ProcessorTabIndex);
    QApplication::processEvents();
    auto *tab = window.findChild<ProcessorTab *>();
    ASSERT_NE(tab, nullptr);
    EXPECT_TRUE(tab->findChildren<QTableView *>().isEmpty()) << "the counter table is gone";
    auto *view = tab->findChild<CircuitView *>();
    ASSERT_NE(view, nullptr);
    // The diagram has the tab's whole area.
    EXPECT_GE(view->height(), tab->height() - 2);
    EXPECT_GE(view->width(), tab->width() - 2);
}

TEST(ProcessorTabMetricsMoved, EveryOldValueIsInTheProfilerWithItsLabel)
{
    // The six counters were the Profiler's own rows already.
    const std::vector<std::pair<ProfilerRow, Provenance>> counters = {
        {ProfilerRow::Cycles, Provenance::Measured}, {ProfilerRow::Retired, Provenance::Measured},
        {ProfilerRow::CPI, Provenance::Modelled},    {ProfilerRow::IPC, Provenance::Modelled},
        {ProfilerRow::Stalls, Provenance::Measured}, {ProfilerRow::Mispredictions, Provenance::Measured}};
    for (const auto &[row, provenance] : counters)
    {
        EXPECT_FALSE(profilerRowLabel(row).isEmpty());
        EXPECT_EQ(profilerRowProvenance(row), provenance) << profilerRowLabel(row).toStdString();
    }

    // The program counter is new there: More metrics > Summary, [Measured].
    MainWindow window;
    window.show();
    MoreMetricsPanel *panel   = window.findChild<ProfilerTab *>()->moreMetrics();
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    window.processorChanged(ProcessorType::RVSS);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/scheduler/load_use_original.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->step();
    manager->step();
    panel->refresh();
    EXPECT_EQ(panel->summarySource(QStringLiteral("Program counter")), QStringLiteral("[Measured]"));
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Program counter")), QStringLiteral("0x00000008"));
    manager->run();
    panel->refresh();
    EXPECT_EQ(panel->summaryValue(QStringLiteral("Program counter")),
              QStringLiteral("0x%1").arg(manager->getProgramCounter(), 8, 16, QLatin1Char('0')));
}
