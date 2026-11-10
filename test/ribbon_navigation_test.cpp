/**
 * @file ribbon_navigation_test.cpp
 * @brief Stage 1 of the ribbon: the strip, the pages and the one writer.
 *
 * Two layers are covered here.
 *
 * RibbonBar/RibbonPage/RibbonGroup are tested standalone, with no window, so
 * the ribbon's own contract does not depend on anything MainWindow does.
 *
 * MainWindow is then tested for the invariant the whole design rests on: the
 * tab strip, the ribbon page and the content stack always agree, whether the
 * switch came from the strip or from code. Before this stage the window
 * exposed no way to read or drive the current tab at all.
 *
 * Note on icons: kites_test links kites_core but not the compiled .qrc
 * objects, so ":/icons/*.svg" does not resolve and every QIcon here is null
 * (ThemeManager likewise reports zero available themes). Icon *tinting* is
 * therefore not assertable at this level and is covered by the out-of-tree
 * GUI smoke harness, which links the resources. What is asserted below is the
 * plumbing: that a theme change reaches the strip and leaves it intact.
 */
#include "ui/main_window/main_window.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"
#include "ui/theme/icon_manager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QListWidget>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>

using namespace Kites;

namespace
{
/// The six tabs, in TabIndex order, as MainWindow builds them.
struct TabSpec
{
    const char *text;
    Icon        icon;
};
const TabSpec kTabs[] = {
    {"Editor", Icon::Editor},     {"Memory", Icon::Memory},
    {"Processor", Icon::Processor}, {"Cache", Icon::Cache},
    {"Compiler", Icon::Compiler}, {"Profiler", Icon::Profiler},
};

/// Counts emissions of a signal, recording the argument. QSignalSpy lives in
/// Qt6::Test, which this project does not link (Core/Widgets/Network/
/// Concurrent only), and adding a module just to count signals is not worth a
/// new dependency.
struct SignalCounter
{
    int        count = 0;
    QList<int> values;
    void       reset()
    {
        count = 0;
        values.clear();
    }
};

RibbonBar *makeSixTabBar(RibbonBar *bar)
{
    for (const TabSpec &spec : kTabs)
        bar->addTab(spec.text, spec.icon);
    return bar;
}

/// The content stack MainWindow keeps its six tab widgets in. Found by type:
/// it is the only QStackedWidget directly under the central widget, and the
/// ribbon's own page stack lives inside the RibbonBar.
QStackedWidget *contentStack(MainWindow &window)
{
    RibbonBar *bar = window.findChild<RibbonBar *>();
    for (QStackedWidget *stack : window.findChildren<QStackedWidget *>())
    {
        if (bar && bar->isAncestorOf(stack))
            continue; // the ribbon's page stack
        if (stack->count() == MainWindow::TabCount)
            return stack;
    }
    return nullptr;
}
} // namespace

// ───────────────────────── RibbonBar, standalone ─────────────────────────

TEST(RibbonBar, AddsOnePagePerTab)
{
    RibbonBar bar;
    makeSixTabBar(&bar);

    EXPECT_EQ(bar.count(), 6);
    for (int index = 0; index < 6; ++index)
    {
        EXPECT_NE(bar.page(index), nullptr) << "no page for tab " << index;
        EXPECT_EQ(bar.tabBar()->tabText(index), QString(kTabs[index].text));
    }
    // Out of range asks for a page that does not exist rather than crashing.
    EXPECT_EQ(bar.page(-1), nullptr);
    EXPECT_EQ(bar.page(6), nullptr);
}

TEST(RibbonBar, PagesStartEmpty)
{
    RibbonBar bar;
    makeSixTabBar(&bar);

    for (int index = 0; index < bar.count(); ++index)
        EXPECT_EQ(bar.page(index)->groupCount(), 0) << "page " << index << " is not empty";
    EXPECT_EQ(bar.persistentGroupCount(), 0);
}

TEST(RibbonBar, StripDrivesThePageStack)
{
    RibbonBar bar;
    makeSixTabBar(&bar);

    for (int index = 5; index >= 0; --index)
    {
        bar.tabBar()->setCurrentIndex(index);
        EXPECT_EQ(bar.currentIndex(), index);
        EXPECT_EQ(bar.currentPage(), bar.page(index))
            << "panel shows a page that does not belong to tab " << index;
    }
}

TEST(RibbonBar, SetCurrentIndexMovesTheStrip)
{
    RibbonBar bar;
    makeSixTabBar(&bar);

    bar.setCurrentIndex(4);
    EXPECT_EQ(bar.tabBar()->currentIndex(), 4);
    EXPECT_EQ(bar.currentPage(), bar.page(4));
}

TEST(RibbonBar, EmitsOncePerRealChangeAndNeverForANoOp)
{
    RibbonBar bar;
    makeSixTabBar(&bar);
    bar.setCurrentIndex(0);

    SignalCounter seen;
    QObject::connect(&bar, &RibbonBar::currentChanged, &bar,
                     [&seen](int index)
                     {
                         ++seen.count;
                         seen.values.append(index);
                     });

    bar.setCurrentIndex(3);
    EXPECT_EQ(seen.count, 1) << "a real change must emit exactly once, not echo";
    ASSERT_EQ(seen.values.size(), 1);
    EXPECT_EQ(seen.values.at(0), 3);

    seen.reset();
    bar.setCurrentIndex(3);
    EXPECT_EQ(seen.count, 0) << "setting the index that is already current must emit nothing";
}

TEST(RibbonBar, RetintSurvivesWithoutResources)
{
    // Guards the plumbing, not the pixels: retintIcons() must walk exactly
    // the tabs that exist and leave the strip's shape alone even when the
    // icon files are unavailable, as they are in this binary.
    RibbonBar bar;
    makeSixTabBar(&bar);
    bar.setCurrentIndex(2);

    bar.retintIcons();

    EXPECT_EQ(bar.count(), 6);
    EXPECT_EQ(bar.currentIndex(), 2);
    for (int index = 0; index < 6; ++index)
        EXPECT_EQ(bar.tabBar()->tabText(index), QString(kTabs[index].text));
}

// ──────────────────── RibbonPage and RibbonGroup ────────────────────

TEST(RibbonPage, GroupsKeepInsertionOrder)
{
    RibbonPage page;
    RibbonGroup *first  = page.addGroup("Run");
    RibbonGroup *second = page.addGroup("File");

    EXPECT_EQ(page.groupCount(), 2);
    EXPECT_EQ(page.group(0), first);
    EXPECT_EQ(page.group(1), second);
    EXPECT_EQ(first->caption(), QStringLiteral("Run"));
    EXPECT_EQ(second->caption(), QStringLiteral("File"));
    EXPECT_EQ(page.group(2), nullptr);
}

TEST(RibbonGroup, ButtonIsDrivenByTheSuppliedAction)
{
    // This is the mechanism every later stage relies on to avoid duplicating
    // command state: the button holds no state of its own.
    RibbonGroup group("Run");
    QAction action("Run");
    action.setCheckable(true);

    QToolButton *button = group.addLarge(&action);
    ASSERT_NE(button, nullptr);
    EXPECT_EQ(button->defaultAction(), &action);
    EXPECT_EQ(static_cast<int>(group.buttons().size()), 1);

    action.setEnabled(false);
    EXPECT_FALSE(button->isEnabled()) << "disabling the action must disable the button";

    action.setChecked(true);
    EXPECT_TRUE(button->isChecked()) << "checked state must follow the action";

    action.setEnabled(true);
    action.setText("Stop");
    EXPECT_EQ(button->text(), QStringLiteral("Stop"))
        << "a relabelled action must relabel its button";
}

TEST(RibbonGroup, ClickingTheButtonTriggersTheAction)
{
    RibbonGroup group("Run");
    QAction action("Run");
    int triggered = 0;
    QObject::connect(&action, &QAction::triggered, &action, [&triggered] { ++triggered; });

    group.addSmall(&action)->click();

    EXPECT_EQ(triggered, 1);
}

TEST(RibbonGroup, CompactHidesLabelsAndRestoresThem)
{
    RibbonGroup group("Run");
    QAction large("Run");
    QAction small("Step");
    setThemedIcon(&large, Icon::Play);
    setThemedIcon(&small, Icon::Step);
    QToolButton *largeButton = group.addLarge(&large);
    QToolButton *smallButton = group.addSmall(&small);

    EXPECT_FALSE(group.isCompact());
    EXPECT_EQ(largeButton->toolButtonStyle(), Qt::ToolButtonTextUnderIcon);
    EXPECT_EQ(smallButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);

    group.setCompact(true);
    EXPECT_TRUE(group.isCompact());
    EXPECT_EQ(largeButton->toolButtonStyle(), Qt::ToolButtonIconOnly);
    EXPECT_EQ(smallButton->toolButtonStyle(), Qt::ToolButtonIconOnly);

    group.setCompact(false);
    EXPECT_EQ(largeButton->toolButtonStyle(), Qt::ToolButtonTextUnderIcon)
        << "leaving compact mode must restore the style the caller asked for";
    EXPECT_EQ(smallButton->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
}

TEST(RibbonGroup, CompactKeepsTheLabelOfAnIconlessButton)
{
    // Hiding the text of a button that has no icon would leave a blank
    // square. Asked of the icon tag, not the pixmap, so the answer does not
    // depend on whether the .qrc objects are linked - they are not here.
    RibbonGroup group("View");
    QAction textOnly("Expanded");
    QToolButton *button = group.addSmall(&textOnly);

    group.setCompact(true);
    EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonTextOnly);

    group.setCompact(false);
    EXPECT_EQ(button->toolButtonStyle(), Qt::ToolButtonTextBesideIcon);
}

TEST(RibbonGroup, ButtonsStayOutOfTheTabChain)
{
    // The approved focus design: the strip is the chrome's single tab stop.
    RibbonGroup group("Run");
    QAction action("Run");
    EXPECT_EQ(group.addLarge(&action)->focusPolicy(), Qt::NoFocus);
}

// ─────────────────── MainWindow: the one writer ───────────────────

TEST(MainWindowRibbon, HasSixTabsAndSixContentPages)
{
    MainWindow window;

    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr) << "the window must own a ribbon";
    EXPECT_EQ(bar->count(), 6);
    EXPECT_EQ(bar->count(), static_cast<int>(MainWindow::TabCount));

    QStackedWidget *stack = contentStack(window);
    ASSERT_NE(stack, nullptr);
    EXPECT_EQ(stack->count(), static_cast<int>(MainWindow::TabCount));

    EXPECT_EQ(window.findChild<QTabBar *>("ribbonTabBar"), bar->tabBar());
}

TEST(MainWindowRibbon, OpensOnTheEditorTab)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    EXPECT_EQ(window.currentTab(), MainWindow::EditorTabIndex);
    EXPECT_EQ(bar->currentIndex(), 0);
    EXPECT_EQ(contentStack(window)->currentIndex(), 0);
}

TEST(MainWindowRibbon, StripSelectionReachesPageAndContent)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    QStackedWidget *stack = contentStack(window);
    ASSERT_NE(stack, nullptr);

    for (int index = 0; index < bar->count(); ++index)
    {
        bar->tabBar()->setCurrentIndex(index); // as a click would
        EXPECT_EQ(bar->currentIndex(), index);
        EXPECT_EQ(bar->currentPage(), bar->page(index)) << "ribbon page lagged at " << index;
        EXPECT_EQ(stack->currentIndex(), index) << "content lagged at " << index;
        EXPECT_EQ(static_cast<int>(window.currentTab()), index);
    }
}

TEST(MainWindowRibbon, ProgrammaticSwitchMovesTheStrip)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    QStackedWidget *stack = contentStack(window);
    ASSERT_NE(stack, nullptr);

    const MainWindow::TabIndex order[] = {
        MainWindow::CacheTabIndex,    MainWindow::ProfilerTabIndex,
        MainWindow::MemoryTabIndex,   MainWindow::CompilerTabIndex,
        MainWindow::ProcessorTabIndex, MainWindow::EditorTabIndex};

    for (MainWindow::TabIndex tab : order)
    {
        window.setCurrentTab(tab);
        const int expected = static_cast<int>(tab);
        EXPECT_EQ(bar->currentIndex(), expected) << "strip did not follow a programmatic switch";
        EXPECT_EQ(bar->currentPage(), bar->page(expected));
        EXPECT_EQ(stack->currentIndex(), expected);
        EXPECT_EQ(window.currentTab(), tab);
    }
}

TEST(MainWindowRibbon, OutOfRangeSwitchChangesNothing)
{
    MainWindow window;
    window.setCurrentTab(MainWindow::CacheTabIndex);

    window.setCurrentTab(static_cast<MainWindow::TabIndex>(-1));
    EXPECT_EQ(window.currentTab(), MainWindow::CacheTabIndex);

    window.setCurrentTab(MainWindow::TabCount);
    EXPECT_EQ(window.currentTab(), MainWindow::CacheTabIndex);
}

TEST(MainWindowRibbon, NoEchoBetweenTheStripAndTheContentStack)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    window.setCurrentTab(MainWindow::EditorTabIndex);

    SignalCounter seen;
    QObject::connect(bar, &RibbonBar::currentChanged, bar,
                     [&seen](int index)
                     {
                         ++seen.count;
                         seen.values.append(index);
                     });

    // A strip interaction: exactly one notification, no bounce back.
    bar->tabBar()->setCurrentIndex(2);
    EXPECT_EQ(seen.count, 1);
    ASSERT_EQ(seen.values.size(), 1);
    EXPECT_EQ(seen.values.at(0), 2);
    EXPECT_EQ(window.currentTab(), MainWindow::ProcessorTabIndex);

    // A programmatic switch: the window is already driving both sides, so the
    // bar's own signal is blocked and cannot re-enter setCurrentTab(). The
    // strip still moves - that is the point of the previous test.
    seen.reset();
    window.setCurrentTab(MainWindow::CompilerTabIndex);
    EXPECT_EQ(seen.count, 0);
    EXPECT_EQ(bar->currentIndex(), static_cast<int>(MainWindow::CompilerTabIndex));

    // Selecting the tab that is already current is a no-op either way.
    seen.reset();
    bar->tabBar()->setCurrentIndex(static_cast<int>(MainWindow::CompilerTabIndex));
    EXPECT_EQ(seen.count, 0);
    EXPECT_EQ(window.currentTab(), MainWindow::CompilerTabIndex);
}

TEST(MainWindowRibbon, SidebarIsGone)
{
    // The sidebar and its outer splitter are what this stage replaced; a
    // leftover would mean two things could select a tab.
    MainWindow window;
    EXPECT_EQ(window.findChild<QListWidget *>(), nullptr)
        << "the QListWidget sidebar must no longer exist";
}

TEST(MainWindowRibbon, ThemeChangeLeavesTheStripIntact)
{
    // Re-tinting the strip is plumbing this binary cannot see the colour of
    // (no .qrc, so every icon is null). What it can check is that the theme
    // signal reaches the ribbon without disturbing the tabs or the selection.
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    window.setCurrentTab(MainWindow::MemoryTabIndex);

    window.themeChangedSlot(QStringLiteral("kites-dark"));

    EXPECT_EQ(bar->count(), 6);
    EXPECT_EQ(bar->currentIndex(), static_cast<int>(MainWindow::MemoryTabIndex));
    EXPECT_EQ(window.currentTab(), MainWindow::MemoryTabIndex);
    EXPECT_EQ(bar->tabBar()->tabText(0), QStringLiteral("Editor"));
    EXPECT_EQ(bar->tabBar()->tabText(5), QStringLiteral("Profiler"));
}
