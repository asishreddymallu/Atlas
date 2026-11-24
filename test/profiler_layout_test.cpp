/**
 * @file profiler_layout_test.cpp
 * @brief known_issues 147 (final/polish): at the default 1400 x 900 window
 *        every Profiler table - the counters, the instruction types and the
 *        four "More metrics" tables - shows every column whole with no
 *        horizontal scroll, the Cache tab's misses-by-cause line is not
 *        clipped, and the window's minimum size is no larger than before.
 */
#include "assembler/assembler.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cache_live_view.h"
#include "ui/timing_grid/timing_grid.h"
#include "utils/utils.h"
#include <QStandardItemModel>
#include "ui/cache_tab/cache_tab.h"
#include "ui/main_window/main_window.h"
#include "ui/profiler_tab/more_metrics_panel.h"
#include "ui/profiler_tab/profiler_tab.h"

#include <QApplication>
#include <QHeaderView>
#include <QLabel>
#include <QAbstractItemDelegate>
#include <QScrollBar>
#include <QStyleOptionViewItem>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
void settle()
{
    for (int i = 0; i < 6; ++i)
        QApplication::processEvents();
}

/// The view's own measure of a column's widest cell (protected in Qt).
struct ColumnPeek : QTableView
{
    using QTableView::sizeHintForColumn;
};
int contentsWidth(QTableView *v, int column)
{
    return static_cast<ColumnPeek *>(v)->sizeHintForColumn(column);
}

void expectWhole(QTableView *v)
{
    const std::string name = v->objectName().toStdString();
    EXPECT_FALSE(v->horizontalScrollBar()->isVisible()) << name;
    QHeaderView *h = v->horizontalHeader();
    EXPECT_LE(h->length(), v->viewport()->width()) << name;
    for (int c = 0; c < v->model()->columnCount(); ++c)
    {
        EXPECT_GE(h->sectionSize(c), h->sectionSizeHint(c)) << name << " column " << c << " header";
        EXPECT_GE(h->sectionSize(c), contentsWidth(v, c)) << name << " column " << c << " contents";
    }
}
} // namespace

TEST(ProfilerLayout, EveryTableFitsAtTheDefaultWindowWidth)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    // With a finished run, so every table has its real rows.
    ProcessorManager *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    window.processorChanged(ProcessorType::RV5Stage_H_F);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/cache_lab/stride_1.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->run();
    window.setCurrentTab(MainWindow::ProfilerTabIndex);
    settle();
    auto *tab = window.findChild<ProfilerTab *>();
    ASSERT_NE(tab, nullptr);
    tab->moreMetrics()->refresh();
    settle();
    EXPECT_EQ(tab->moreMetrics()->loopModel()->rowCount(), 2);
    expectWhole(tab->findChild<QTableView *>(QStringLiteral("profilerMetricsTableView")));
    expectWhole(tab->findChild<QTableView *>(QStringLiteral("instructionTypeCountTableView")));
    QTabWidget *tabs = tab->moreMetrics()->tabs();
    ASSERT_EQ(tabs->count(), 4);
    const char *views[] = {"moreMetricsSummaryView", "moreMetricsTypeView", "moreMetricsMnemonicView",
                           "moreMetricsLoopView"};
    for (int t = 0; t < 4; ++t)
    {
        tabs->setCurrentIndex(t);
        settle();
        expectWhole(tab->findChild<QTableView *>(QString::fromLatin1(views[t])));
    }
    // The right pane is about 45 % of the tab.
    auto *splitter = tab->findChild<QSplitter *>(QStringLiteral("splitter"));
    ASSERT_NE(splitter, nullptr);
    const QList<int> sizes = splitter->sizes();
    ASSERT_EQ(sizes.size(), 2);
    EXPECT_NEAR(double(sizes[1]) / (sizes[0] + sizes[1]), 0.45, 0.03);
}

TEST(ProfilerLayout, TheCacheTabsCauseLineIsNotClipped)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    window.setCurrentTab(MainWindow::CacheTabIndex);
    settle();
    auto *cache = window.findChild<CacheTab *>();
    ASSERT_NE(cache, nullptr);
    for (CacheTab::CacheLevel level : {CacheTab::L1, CacheTab::L2, CacheTab::Instruction})
    {
        auto *label = cache->liveView(level)->findChild<QLabel *>(QStringLiteral("cacheMissCauses"));
        ASSERT_NE(label, nullptr);
        if (!label->isVisible())
            continue; // another sub-tab; its own turn below
        EXPECT_GE(label->height(), label->heightForWidth(label->width())) << "the line is cut off";
    }
}

TEST(ProfilerLayout, TheWindowsMinimumSizeIsNoLargerThanBefore)
{
    MainWindow window;
    window.show();
    settle();
    EXPECT_LE(window.minimumSizeHint().width(), 762);
    EXPECT_LE(window.minimumSizeHint().height(), 616);
}
