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
#include "ui/editor_tab/editor_tab.h"
#include "ui/main_window/main_window.h"
#include "ui/problems_panel/problems_dock.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"
#include "ui/theme/icon_manager.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSpinBox>
#include <QToolBar>
#include <QWidgetAction>
#include <algorithm>
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

// ════════════════════ Stage 2: persistent groups and the Editor page ═══════
//
// These reach into the live window rather than a standalone RibbonBar,
// because what matters is that the ribbon shows the *same* QAction objects
// the menus and the toolbar use, which only the assembled window can show.

namespace
{
/// The actions on a toolbar, skipping separators and the QWidgetAction
/// wrapper, in order: Processor, Run, Debug, Pause, Step, Undo, Redo.
QList<QAction *> toolbarActions(MainWindow &window)
{
    QList<QAction *> result;
    const QList<QToolBar *> bars = window.findChildren<QToolBar *>();
    if (bars.isEmpty())
        return result;
    for (QAction *a : bars.first()->actions())
        if (!a->isSeparator() && !qobject_cast<QWidgetAction *>(a))
            result.append(a);
    return result;
}

RibbonGroup *persistentGroupNamed(RibbonBar *bar, const QString &caption)
{
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        if (bar->persistentGroup(i)->caption() == caption)
            return bar->persistentGroup(i);
    return nullptr;
}

/// Every action bound to a button anywhere on the ribbon.
QList<QAction *> ribbonActions(RibbonBar *bar)
{
    QList<QAction *> result;
    auto collect = [&result](RibbonGroup *group)
    {
        for (QToolButton *button : group->buttons())
            if (QAction *a = button->defaultAction())
                result.append(a);
    };
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        collect(bar->persistentGroup(i));
    for (int p = 0; p < bar->count(); ++p)
        for (RibbonGroup *group : bar->page(p)->groups())
            collect(group);
    return result;
}

/// The execution-speed spin box, named by its suffix rather than by
/// findChild, which is depth-first and would return a cache config field.
QSpinBox *speedSpin(MainWindow &window)
{
    for (QSpinBox *spin : window.findChildren<QSpinBox *>())
        if (spin->suffix() == QStringLiteral(" ms"))
            return spin;
    return nullptr;
}

QAction *menuEntry(MainWindow &window, int menuIndex, int entryIndex)
{
    const QList<QAction *> menus = window.menuBar()->actions();
    if (menuIndex < 0 || menuIndex >= menus.size())
        return nullptr;
    QMenu *menu = menus.at(menuIndex)->menu();
    if (!menu)
        return nullptr;
    QList<QAction *> entries;
    for (QAction *a : menu->actions())
        if (!a->isSeparator())
            entries.append(a);
    return entries.value(entryIndex);
}
} // namespace

TEST(RibbonStage2, PersistentGroupsExistOnceNotPerPage)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    EXPECT_EQ(bar->persistentGroupCount(), 2);
    EXPECT_NE(persistentGroupNamed(bar, "Run"), nullptr);
    EXPECT_NE(persistentGroupNamed(bar, "File"), nullptr);

    // The whole point of the persistent area: one set of buttons, not six.
    for (int page = 0; page < bar->count(); ++page)
        for (RibbonGroup *group : bar->page(page)->groups())
            EXPECT_NE(group->caption(), QStringLiteral("Run"))
                << "page " << page << " has its own copy of the Run group";
}

TEST(RibbonStage2, OnlyTheEditorPageHasAGroup)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    RibbonPage *editorPage = bar->page(MainWindow::EditorTabIndex);
    ASSERT_NE(editorPage, nullptr);
    ASSERT_EQ(editorPage->groupCount(), 1);
    EXPECT_EQ(editorPage->group(0)->caption(), QStringLiteral("View"));

    for (int page = 0; page < bar->count(); ++page)
        if (page != MainWindow::EditorTabIndex)
            EXPECT_EQ(bar->page(page)->groupCount(), 0)
                << "page " << page << " should still be empty in Stage 2";
}

TEST(RibbonStage2, RibbonButtonsUseTheVerySameActionsAsTheToolbarAndMenus)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    const QList<QAction *> toolbar = toolbarActions(window);
    ASSERT_EQ(toolbar.size(), 7);
    const QList<QAction *> onRibbon = ribbonActions(bar);

    // Pointer identity, not equal text: a clone would compare equal by label
    // and still be a second copy of the state.
    for (QAction *a : toolbar)
        EXPECT_TRUE(onRibbon.contains(a))
            << "toolbar action '" << a->text().toStdString() << "' is not the object on the ribbon";

    QAction *open = menuEntry(window, 0, 0); // File > Open
    QAction *save = menuEntry(window, 0, 1); // File > Save
    ASSERT_NE(open, nullptr);
    ASSERT_NE(save, nullptr);
    EXPECT_TRUE(onRibbon.contains(open));
    EXPECT_TRUE(onRibbon.contains(save));

    // View > Problems is the dock's own toggleViewAction.
    QAction *problems = menuEntry(window, window.menuBar()->actions().size() - 1, 0);
    ASSERT_NE(problems, nullptr);
    EXPECT_TRUE(onRibbon.contains(problems));
    EXPECT_EQ(problems, window.findChild<ProblemsDock *>()->toggleViewAction());

    // No action appears on a ribbon button twice.
    for (QAction *a : onRibbon)
        EXPECT_EQ(std::count(onRibbon.cbegin(), onRibbon.cend(), a), 1)
            << "'" << a->text().toStdString() << "' is on two ribbon buttons";
}

TEST(RibbonStage2, ButtonStateFollowsTheSharedAction)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    RibbonGroup *run = persistentGroupNamed(bar, "Run");
    ASSERT_NE(run, nullptr);

    QToolButton *runButton = nullptr;
    QToolButton *stepButton = nullptr;
    for (QToolButton *button : run->buttons())
    {
        if (button->defaultAction() == toolbarActions(window).at(1))
            runButton = button;
        if (button->defaultAction() == toolbarActions(window).at(4))
            stepButton = button;
    }
    ASSERT_NE(runButton, nullptr);
    ASSERT_NE(stepButton, nullptr);

    // Step starts disabled, exactly as the toolbar shows it.
    EXPECT_FALSE(stepButton->isEnabled());
    EXPECT_TRUE(runButton->isEnabled());

    QAction *runAction = runButton->defaultAction();
    runAction->setEnabled(false);
    EXPECT_FALSE(runButton->isEnabled()) << "disabling the action must disable the ribbon button";
    runAction->setEnabled(true);

    runAction->setText("Stop");
    EXPECT_EQ(runButton->text(), QStringLiteral("Stop"));
    runAction->setText("Run");

    QAction *problems = window.findChild<ProblemsDock *>()->toggleViewAction();
    QToolButton *problemsButton = nullptr;
    for (QToolButton *button : persistentGroupNamed(bar, "File")->buttons())
        if (button->defaultAction() == problems)
            problemsButton = button;
    ASSERT_NE(problemsButton, nullptr);
    EXPECT_TRUE(problemsButton->isCheckable());
    EXPECT_EQ(problemsButton->isChecked(), problems->isChecked());
    problems->setChecked(!problems->isChecked());
    EXPECT_EQ(problemsButton->isChecked(), problems->isChecked())
        << "checked state must follow the action";
}

TEST(RibbonStage2, SpeedSpinBoxMovedIntactAndStillReachesTheWindow)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    QSpinBox *spin = speedSpin(window);
    ASSERT_NE(spin, nullptr) << "the execution-speed spin box is gone";

    // Unchanged: same range, same default, same suffix.
    EXPECT_EQ(spin->minimum(), 1);
    EXPECT_EQ(spin->maximum(), 10000);
    EXPECT_EQ(spin->value(), 1000);

    // It lives in the ribbon's Run group now, and in exactly one place.
    EXPECT_TRUE(bar->isAncestorOf(spin)) << "the spin box is not inside the ribbon";
    EXPECT_EQ(window.findChildren<QSpinBox *>().size() > 0, true);
    int msSpins = 0;
    for (QSpinBox *candidate : window.findChildren<QSpinBox *>())
        if (candidate->suffix() == QStringLiteral(" ms"))
            ++msSpins;
    EXPECT_EQ(msSpins, 1) << "the spin box was copied rather than moved";

    // The toolbar no longer carries it.
    const QList<QToolBar *> bars = window.findChildren<QToolBar *>();
    ASSERT_FALSE(bars.isEmpty());
    for (QAction *a : bars.first()->actions())
        EXPECT_EQ(qobject_cast<QWidgetAction *>(a), nullptr)
            << "the toolbar still holds a widget action";

    // The value still goes somewhere: disconnecting with the window as the
    // receiver only succeeds if such a connection existed. Destructive, which
    // is fine on this throwaway window, and it is the only public way to ask.
    // The last hop - the lambda calling ProcessorManager::setStepDelay - is
    // untouched by this work and cannot be observed without a getter on the
    // processor, which is out of bounds for this task.
    spin->setValue(42);
    EXPECT_EQ(spin->value(), 42);
    EXPECT_TRUE(QObject::disconnect(spin, qOverload<int>(&QSpinBox::valueChanged), &window, nullptr))
        << "the spin box is not connected to the window any more";
}

TEST(RibbonStage2, ViewButtonsMirrorTheInTabButtonsBothWays)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *editorTab = window.findChild<EditorTab *>();
    ASSERT_NE(editorTab, nullptr);

    RibbonGroup *view = bar->page(MainWindow::EditorTabIndex)->group(0);
    ASSERT_NE(view, nullptr);
    ASSERT_EQ(view->buttons().size(), 2);
    QToolButton *editorButton   = view->buttons().at(0);
    QToolButton *expandedButton = view->buttons().at(1);

    auto *inTabEditor   = editorTab->findChild<QPushButton *>("editorViewButton");
    auto *inTabExpanded = editorTab->findChild<QPushButton *>("expandedViewButton");
    ASSERT_NE(inTabEditor, nullptr);
    ASSERT_NE(inTabExpanded, nullptr);

    // Starting state agrees.
    EXPECT_TRUE(editorButton->isChecked());
    EXPECT_FALSE(expandedButton->isChecked());
    EXPECT_TRUE(inTabEditor->isChecked());

    // tab -> ribbon
    inTabExpanded->click();
    EXPECT_TRUE(editorTab->isExpandedView());
    EXPECT_TRUE(expandedButton->isChecked()) << "ribbon did not follow the in-tab button";
    EXPECT_FALSE(editorButton->isChecked());

    inTabEditor->click();
    EXPECT_FALSE(editorTab->isExpandedView());
    EXPECT_TRUE(editorButton->isChecked());
    EXPECT_FALSE(expandedButton->isChecked());

    // ribbon -> tab
    expandedButton->click();
    EXPECT_TRUE(editorTab->isExpandedView()) << "the tab did not follow the ribbon button";
    EXPECT_TRUE(inTabExpanded->isChecked());
    EXPECT_FALSE(inTabEditor->isChecked());

    editorButton->click();
    EXPECT_FALSE(editorTab->isExpandedView());
    EXPECT_TRUE(inTabEditor->isChecked());

    // The run lock reaches the ribbon too, or the ribbon would be a way
    // around it while the editor is read-only.
    editorTab->setExpandedLocked(true);
    EXPECT_TRUE(editorTab->isExpandedView());
    EXPECT_TRUE(expandedButton->isChecked());
    EXPECT_FALSE(editorButton->isEnabled()) << "the ribbon can still leave the locked view";
    EXPECT_FALSE(inTabEditor->isEnabled());

    editorTab->setExpandedLocked(false);
    EXPECT_TRUE(editorButton->isEnabled());
}

TEST(RibbonStage2, EveryRibbonButtonStaysOutOfTheTabChain)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    int checked = 0;
    auto check = [&checked](RibbonGroup *group)
    {
        for (QToolButton *button : group->buttons())
        {
            EXPECT_EQ(button->focusPolicy(), Qt::NoFocus)
                << "'" << button->text().toStdString() << "' is in the tab chain";
            ++checked;
        }
    };
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        check(bar->persistentGroup(i));
    for (int p = 0; p < bar->count(); ++p)
        for (RibbonGroup *group : bar->page(p)->groups())
            check(group);

    EXPECT_GT(checked, 0) << "no buttons were examined";
    EXPECT_EQ(bar->tabBar()->focusPolicy(), Qt::TabFocus);
}

TEST(RibbonStage2, EveryRibbonActionWithAnIconCarriesItsThemeTag)
{
    // The window-wide re-tint pass walks findChildren<QAction*>() and keys off
    // kitesIconId, so an icon set any other way would freeze at the theme it
    // was built under. This also pins the two deliberately icon-less View
    // actions: give either one an icon without tagging it and this fails.
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    const QList<QAction *> actions = ribbonActions(bar);
    EXPECT_GE(actions.size(), 12);

    int tagged = 0;
    int untaggedAndIconless = 0;
    for (QAction *a : actions)
    {
        const bool hasTag = themedIconOf(a) != Icon::IconCount;
        if (hasTag)
        {
            ++tagged;
            EXPECT_FALSE(themedIconSizeOf(a).isEmpty());
            continue;
        }
        EXPECT_TRUE(a->icon().isNull())
            << "'" << a->text().toStdString()
            << "' has an icon that setThemedIcon did not set, so a theme change will not re-tint it";
        ++untaggedAndIconless;
    }
    EXPECT_GT(tagged, 0);
    // Problems (the dock's toggle) plus the two View actions carry no icon.
    EXPECT_EQ(untaggedAndIconless, 3);
}

TEST(RibbonStage2, LargeButtonsAskForThirtyTwoPixelIcons)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    const QList<QAction *> toolbar = toolbarActions(window);
    ASSERT_EQ(toolbar.size(), 7);
    QAction *runAction   = toolbar.at(1);
    QAction *debugAction = toolbar.at(2);

    EXPECT_EQ(themedIconSizeOf(runAction), QSize(32, 32));
    EXPECT_EQ(themedIconSizeOf(debugAction), QSize(32, 32));
    // A small one is still tagged at the chrome size.
    EXPECT_EQ(themedIconSizeOf(toolbar.at(4)), QSize(20, 20));
}
