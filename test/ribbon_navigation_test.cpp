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
#include "ui/memory_tab/memory_tab.h"
#include "ui/cache_tab/cache_config_widget.h"
#include "ui/compiler_tab/compiler_tab.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/processor_tab/circuit_scene.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/register_table/register_container.h"
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
#include <QComboBox>
#include <QApplication>
#include <QClipboard>
#include <QGraphicsView>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPointer>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableView>
#include <QTabWidget>
#include <QToolBar>
#include <QWidgetAction>
#include <algorithm>
#include <functional>
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
RibbonGroup *persistentGroupNamed(RibbonBar *bar, const QString &caption)
{
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        if (bar->persistentGroup(i)->caption() == caption)
            return bar->persistentGroup(i);
    return nullptr;
}

/// The seven execution actions, in the persistent Run group's order:
/// Run, Debug Run, Pause, Step, Undo, Redo, Processor.
///
/// These were the old main toolbar's, and until Stage 4A the tests took
/// them from it. The toolbar is gone; the ribbon shows the very same
/// objects, so the group is where they are named now. Note the orders
/// differ - the toolbar read Processor | Run Debug | Pause Step |
/// Undo Redo - so nothing here may be addressed by the old index.
QList<QAction *> executionActions(RibbonBar *bar)
{
    QList<QAction *> result;
    RibbonGroup     *run = persistentGroupNamed(bar, "Run");
    if (!run)
        return result;
    for (QToolButton *button : run->buttons())
        if (QAction *a = button->defaultAction())
            result.append(a);
    return result;
}

/// One of those by its label. Labels are the ones the window starts with;
/// Run and Pause reword themselves at runtime, which is why production code
/// reads the icon tag instead and these tests only ask at construction.
QAction *executionAction(RibbonBar *bar, const QString &label)
{
    for (QAction *a : executionActions(bar))
        if (a->text() == label)
            return a;
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

namespace
{
/// Every ribbon page's groups, in order. The persistent groups (Run, File)
/// are deliberately absent: they live beside the pages, not on them, and
/// PersistentGroupsExistOnceNotPerPage is what guards that.
///
/// This table is the ribbon's inventory. Adding a group without listing it
/// here fails, which is the point - nothing reaches the ribbon unnoticed.
struct PageGroups
{
    MainWindow::TabIndex page;
    QStringList          captions;
};
/// The ribbon actions that deliberately carry no icon, by label. Everything
/// else on the ribbon must be tagged with kitesIconId so the theme re-tint
/// pass reaches it; see EveryRibbonActionWithAnIconCarriesItsThemeTag.
const QStringList kIconlessRibbonActions = {
    "Problems", // the dock's own toggleViewAction, which has never had one
    "Editor",     "Expanded", // Editor page, View
    "Search",     "Hex",      "Binary", "Decimal", // Memory page
    "Wires Stay Active",                               // Processor page, Datapath
    "Binary",     "Hexadecimal", "Decimal/Float",      // Processor page, Registers
    "Instruction", "L1",     "L2",                     // Cache page, Level
    "Copy to Clipboard",                               // Compiler page, Output
};

const QList<PageGroups> kPageGroups = {
    {MainWindow::EditorTabIndex, {"View"}},
    {MainWindow::MemoryTabIndex, {"Find", "Display"}},
    {MainWindow::ProcessorTabIndex, {"Datapath", "Registers"}},
    {MainWindow::CacheTabIndex, {"Level"}},
    {MainWindow::CompilerTabIndex, {"Build", "Output"}},
    {MainWindow::ProfilerTabIndex, {}},
};
} // namespace

TEST(RibbonStage2, EachPageHasExactlyTheGroupsItShould)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    ASSERT_EQ(kPageGroups.size(), bar->count());

    for (const PageGroups &spec : kPageGroups)
    {
        RibbonPage *page = bar->page(static_cast<int>(spec.page));
        ASSERT_NE(page, nullptr) << "no page for tab " << spec.page;

        QStringList captions;
        for (RibbonGroup *group : page->groups())
            captions << group->caption();
        EXPECT_EQ(captions, spec.captions) << "wrong groups on page " << spec.page;
    }
}

TEST(RibbonStage2, RibbonButtonsUseTheVerySameActionsAsTheMenus)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    const QList<QAction *> execution = executionActions(bar);
    ASSERT_EQ(execution.size(), 7);
    const QList<QAction *> onRibbon = ribbonActions(bar);

    // Pointer identity, not equal text: a clone would compare equal by label
    // and still be a second copy of the state.
    for (QAction *a : execution)
        EXPECT_TRUE(onRibbon.contains(a))
            << "execution action '" << a->text().toStdString() << "' is not on a ribbon button";

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
        if (button->defaultAction() == executionAction(bar, "Run"))
            runButton = button;
        if (button->defaultAction() == executionAction(bar, "Step"))
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

    // There is no toolbar left to carry it, and nothing wrapped it back up
    // in a QWidgetAction somewhere else.
    EXPECT_TRUE(window.findChildren<QToolBar *>().isEmpty()) << "a QToolBar is back";
    EXPECT_TRUE(window.findChildren<QWidgetAction *>().isEmpty())
        << "the spin box is wrapped in a widget action again";

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
    EXPECT_EQ(untaggedAndIconless, kIconlessRibbonActions.size());

    // ...and they are the ones we chose to leave bare, not an icon quietly
    // lost. The icon set has nothing that says "search", "binary" or "cache
    // level", and reusing a tab's icon for a different idea reads worse than
    // a plain text button.
    QStringList bare;
    for (QAction *a : actions)
        if (themedIconOf(a) == Icon::IconCount)
            bare << a->text();
    bare.sort();
    QStringList expected = kIconlessRibbonActions;
    expected.sort();
    EXPECT_EQ(bare, expected);
}

TEST(RibbonStage2, LargeButtonsAskForThirtyTwoPixelIcons)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    QAction *runAction   = executionAction(bar, "Run");
    QAction *debugAction = executionAction(bar, "Debug Run");
    QAction *stepAction  = executionAction(bar, "Step");
    ASSERT_NE(runAction, nullptr);
    ASSERT_NE(debugAction, nullptr);
    ASSERT_NE(stepAction, nullptr);

    EXPECT_EQ(themedIconSizeOf(runAction), QSize(32, 32));
    EXPECT_EQ(themedIconSizeOf(debugAction), QSize(32, 32));
    // A small one is still tagged at the chrome size.
    EXPECT_EQ(themedIconSizeOf(stepAction), QSize(20, 20));
}

// ═══════════════════ Stage 3: the other five ribbon pages ═════════════════
//
// Every control here mirrors a widget that stays inside its tab. The tests
// below always ask the *widget* what happened, never the ribbon action, so a
// button that only looked right while quietly holding its own copy of the
// state would fail.

namespace
{
/// The ribbon group with this caption on `page`, or null.
RibbonGroup *pageGroupNamed(RibbonBar *bar, MainWindow::TabIndex page, const QString &caption)
{
    for (RibbonGroup *group : bar->page(static_cast<int>(page))->groups())
        if (group->caption() == caption)
            return group;
    return nullptr;
}

/// The button in `group` labelled `text`. Ribbon buttons take their text
/// from their action, so this is asking the action by label.
QToolButton *buttonNamed(RibbonGroup *group, const QString &text)
{
    if (!group)
        return nullptr;
    for (QToolButton *button : group->buttons())
        if (button->text() == text)
            return button;
    return nullptr;
}

/// Exactly one button in the group is checked, and it is this one.
void expectOnlyChecked(RibbonGroup *group, const QString &text)
{
    for (QToolButton *button : group->buttons())
        EXPECT_EQ(button->isChecked(), button->text() == text)
            << "'" << button->text().toStdString() << "' in group '"
            << group->caption().toStdString() << "'";
}
} // namespace

TEST(RibbonStage3, MemoryDisplayBaseReachesTheComboAndTheModel)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *memoryTab = window.findChild<MemoryTab *>();
    ASSERT_NE(memoryTab, nullptr);

    auto *combo = memoryTab->findChild<QComboBox *>("comboBox");
    auto *view  = memoryTab->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(combo, nullptr);
    ASSERT_NE(view, nullptr);
    ASSERT_NE(view->model(), nullptr);

    RibbonGroup *display = pageGroupNamed(bar, MainWindow::MemoryTabIndex, "Display");
    ASSERT_NE(display, nullptr);
    ASSERT_EQ(display->buttons().size(), 3);

    // The Double Word column carries the base's prefix, so the model tells us
    // what base it is really rendering in - not just what the combo says.
    auto doubleWordText = [view] { return view->model()->index(0, 1).data().toString(); };

    // Starting state: the combo's first entry and the model's default agree.
    EXPECT_EQ(combo->currentText(), QStringLiteral("Hex"));
    EXPECT_TRUE(doubleWordText().startsWith(QStringLiteral("0x")));
    expectOnlyChecked(display, "Hex");

    QToolButton *binary = buttonNamed(display, "Binary");
    ASSERT_NE(binary, nullptr);
    binary->click();
    EXPECT_EQ(combo->currentText(), QStringLiteral("Binary")) << "the tab's combo did not move";
    EXPECT_EQ(memoryTab->displayBase(), Base::Binary);
    EXPECT_TRUE(doubleWordText().startsWith(QStringLiteral("0b")))
        << "the model is not rendering in binary: " << doubleWordText().toStdString();
    expectOnlyChecked(display, "Binary");

    QToolButton *decimal = buttonNamed(display, "Decimal");
    ASSERT_NE(decimal, nullptr);
    decimal->click();
    EXPECT_EQ(combo->currentText(), QStringLiteral("Decimal"));
    EXPECT_EQ(memoryTab->displayBase(), Base::Decimal);
    // Decimal is the one base with no prefix. A zeroed cell renders "0x0",
    // "0b0" and "0" in the three bases, so the prefix is what separates them.
    EXPECT_FALSE(doubleWordText().startsWith(QStringLiteral("0x")))
        << doubleWordText().toStdString();
    EXPECT_FALSE(doubleWordText().startsWith(QStringLiteral("0b")))
        << doubleWordText().toStdString();
    expectOnlyChecked(display, "Decimal");
}

TEST(RibbonStage3, MemoryDisplayBaseMirrorsBackFromTheCombo)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *memoryTab = window.findChild<MemoryTab *>();
    ASSERT_NE(memoryTab, nullptr);
    auto *combo = memoryTab->findChild<QComboBox *>("comboBox");
    ASSERT_NE(combo, nullptr);

    RibbonGroup *display = pageGroupNamed(bar, MainWindow::MemoryTabIndex, "Display");
    ASSERT_NE(display, nullptr);

    // Driving the widget the user would actually touch.
    combo->setCurrentText(QStringLiteral("Binary"));
    expectOnlyChecked(display, "Binary");

    combo->setCurrentText(QStringLiteral("Decimal"));
    expectOnlyChecked(display, "Decimal");

    combo->setCurrentText(QStringLiteral("Hex"));
    expectOnlyChecked(display, "Hex");
}

TEST(RibbonStage3, MemorySearchPressesTheTabsOwnButton)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *memoryTab = window.findChild<MemoryTab *>();
    ASSERT_NE(memoryTab, nullptr);

    auto *searchBar    = memoryTab->findChild<QLineEdit *>("searchBar");
    auto *searchButton = memoryTab->findChild<QPushButton *>("searchButton");
    auto *view         = memoryTab->findChild<QTableView *>("memoryTableView");
    ASSERT_NE(searchBar, nullptr);
    ASSERT_NE(searchButton, nullptr);
    ASSERT_NE(view, nullptr);

    // The in-tab button is still the only thing that runs a search.
    SignalCounter clicks;
    QObject::connect(searchButton, &QPushButton::clicked, &window, [&clicks] { ++clicks.count; });

    RibbonGroup *find = pageGroupNamed(bar, MainWindow::MemoryTabIndex, "Find");
    ASSERT_NE(find, nullptr);
    QToolButton *search = buttonNamed(find, "Search");
    ASSERT_NE(search, nullptr);

    searchBar->setText(QStringLiteral("0x100"));
    const QString before = view->model()->index(0, 0).data().toString();

    search->click();

    EXPECT_EQ(clicks.count, 1) << "the ribbon did not go through the tab's own button";
    const QString after = view->model()->index(0, 0).data().toString();
    EXPECT_NE(after, before) << "the table did not move to the searched address";
    EXPECT_EQ(after, QStringLiteral("0x100"));
}

TEST(RibbonStage3, WiresStayActiveIsTheMenusOwnAction)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    RibbonGroup *datapath = pageGroupNamed(bar, MainWindow::ProcessorTabIndex, "Datapath");
    ASSERT_NE(datapath, nullptr);
    ASSERT_EQ(datapath->buttons().size(), 1);
    QAction *onRibbon = datapath->buttons().first()->defaultAction();
    ASSERT_NE(onRibbon, nullptr);

    // Settings > Processor > Wires Stay Active. Found by walking the menus
    // rather than by label, so this is the object the menu shows.
    QAction *fromMenu = nullptr;
    for (QAction *menuAction : window.menuBar()->actions())
    {
        QMenu *menu = menuAction->menu();
        if (!menu)
            continue;
        for (QAction *entry : menu->actions())
        {
            QMenu *submenu = entry->menu();
            if (!submenu)
                continue;
            for (QAction *sub : submenu->actions())
                if (sub->text() == QStringLiteral("Wires Stay Active"))
                    fromMenu = sub;
        }
    }
    ASSERT_NE(fromMenu, nullptr) << "the menu entry is gone";

    // Pointer identity: one action, two places that show it.
    EXPECT_EQ(onRibbon, fromMenu);
    EXPECT_TRUE(onRibbon->isCheckable());
    EXPECT_TRUE(onRibbon->shortcut().isEmpty()) << "nothing new on the ribbon gets a shortcut";
}

TEST(RibbonStage3, WiresStayActiveReachesTheCircuitSceneFromTheRibbon)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    // The scene through the view that shows it. ProcessorManager is not a
    // child of the window (it is moved to its own thread), so the tab's
    // graphics view is the way to reach what the setting actually drives.
    auto *processorTab = window.findChild<ProcessorTab *>();
    ASSERT_NE(processorTab, nullptr);
    auto *view = processorTab->findChild<QGraphicsView *>();
    ASSERT_NE(view, nullptr);
    auto *scene = qobject_cast<CircuitScene *>(view->scene());
    ASSERT_NE(scene, nullptr) << "the processor tab is not showing a circuit scene";

    QToolButton *button =
        pageGroupNamed(bar, MainWindow::ProcessorTabIndex, "Datapath")->buttons().first();

    const bool before = scene->getWireStayActive();
    button->click();
    EXPECT_NE(scene->getWireStayActive(), before) << "the ribbon did not reach the scene";
    EXPECT_EQ(button->isChecked(), scene->getWireStayActive());

    button->click();
    EXPECT_EQ(scene->getWireStayActive(), before);
    EXPECT_EQ(button->isChecked(), scene->getWireStayActive());
}

TEST(RibbonStage3, RegisterDisplayBaseReachesTheComboAndTheModel)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *registers = window.findChild<RegisterContainer *>();
    ASSERT_NE(registers, nullptr);

    auto *combo = registers->findChild<QComboBox *>("displayType");
    ASSERT_NE(combo, nullptr);
    const QList<QTableView *> views = registers->findChildren<QTableView *>();
    ASSERT_FALSE(views.isEmpty());
    // Column 2 is "Value", the only one the base changes.
    auto valueText = [&views] { return views.first()->model()->index(0, 2).data().toString(); };

    RibbonGroup *group = pageGroupNamed(bar, MainWindow::ProcessorTabIndex, "Registers");
    ASSERT_NE(group, nullptr);
    ASSERT_EQ(group->buttons().size(), 3);

    // The constructor selects Hexadecimal, and the ribbon starts in step.
    EXPECT_EQ(combo->currentText(), QStringLiteral("Hexadecimal"));
    EXPECT_TRUE(valueText().startsWith(QStringLiteral("0x")));
    expectOnlyChecked(group, "Hexadecimal");

    buttonNamed(group, "Binary")->click();
    EXPECT_EQ(combo->currentText(), QStringLiteral("Binary")) << "the panel's combo did not move";
    EXPECT_EQ(registers->displayBase(), Base::Binary);
    EXPECT_TRUE(valueText().startsWith(QStringLiteral("0b")))
        << "the model is not rendering in binary: " << valueText().toStdString();
    expectOnlyChecked(group, "Binary");

    buttonNamed(group, "Decimal/Float")->click();
    EXPECT_EQ(combo->currentText(), QStringLiteral("Decimal/Float"));
    EXPECT_EQ(registers->displayBase(), Base::Decimal);
    EXPECT_FALSE(valueText().startsWith(QStringLiteral("0x"))) << valueText().toStdString();
    EXPECT_FALSE(valueText().startsWith(QStringLiteral("0b"))) << valueText().toStdString();
    expectOnlyChecked(group, "Decimal/Float");
}

TEST(RibbonStage3, RegisterDisplayBaseMirrorsBackFromTheCombo)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *registers = window.findChild<RegisterContainer *>();
    ASSERT_NE(registers, nullptr);
    auto *combo = registers->findChild<QComboBox *>("displayType");
    ASSERT_NE(combo, nullptr);

    RibbonGroup *group = pageGroupNamed(bar, MainWindow::ProcessorTabIndex, "Registers");
    ASSERT_NE(group, nullptr);

    combo->setCurrentText(QStringLiteral("Binary"));
    expectOnlyChecked(group, "Binary");

    combo->setCurrentText(QStringLiteral("Decimal/Float"));
    expectOnlyChecked(group, "Decimal/Float");

    combo->setCurrentText(QStringLiteral("Hexadecimal"));
    expectOnlyChecked(group, "Hexadecimal");
}

TEST(RibbonStage3, CacheLevelMovesTheInnerTabBarAndFollowsIt)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *cacheTab = window.findChild<CacheTab *>();
    ASSERT_NE(cacheTab, nullptr);

    auto *inner = cacheTab->findChild<QTabWidget *>("tabWidget");
    ASSERT_NE(inner, nullptr);
    ASSERT_EQ(inner->count(), 3);

    RibbonGroup *level = pageGroupNamed(bar, MainWindow::CacheTabIndex, "Level");
    ASSERT_NE(level, nullptr);
    ASSERT_EQ(level->buttons().size(), 3);

    // The inner bar's page order is not the CacheLevel enum's order, which
    // is exactly the mistake these assertions are here to catch.
    EXPECT_EQ(inner->tabText(0), QStringLiteral("Instruction Cache"));
    EXPECT_EQ(inner->tabText(1), QStringLiteral("L1 Cache"));
    EXPECT_EQ(inner->tabText(2), QStringLiteral("L2 Cache"));

    // Starting state agrees: the tab widget opens on the instruction cache.
    EXPECT_EQ(inner->currentIndex(), 0);
    EXPECT_EQ(cacheTab->currentCacheLevel(), CacheTab::Instruction);
    expectOnlyChecked(level, "Instruction");

    // ribbon -> tab
    buttonNamed(level, "L2")->click();
    EXPECT_EQ(inner->currentIndex(), 2) << "the inner tab bar did not move";
    EXPECT_EQ(inner->tabText(inner->currentIndex()), QStringLiteral("L2 Cache"));
    EXPECT_EQ(cacheTab->currentCacheLevel(), CacheTab::L2);
    expectOnlyChecked(level, "L2");

    buttonNamed(level, "L1")->click();
    EXPECT_EQ(inner->tabText(inner->currentIndex()), QStringLiteral("L1 Cache"));
    EXPECT_EQ(cacheTab->currentCacheLevel(), CacheTab::L1);
    expectOnlyChecked(level, "L1");

    // tab -> ribbon
    inner->setCurrentIndex(0);
    EXPECT_EQ(cacheTab->currentCacheLevel(), CacheTab::Instruction);
    expectOnlyChecked(level, "Instruction");

    inner->setCurrentIndex(2);
    expectOnlyChecked(level, "L2");
}

TEST(RibbonStage3, CachePageOffersNoPolicyButton)
{
    // "Custom policy..." is deliberately absent: CacheConfigWidget's handler
    // assumes the replacement-policy combo has already moved to Custom, and
    // calling it from anywhere else leaves the combo and the cache config
    // disagreeing. If a Policy group is ever added, it has to come with the
    // combo move, not just the dialog - so this fails until that is thought
    // through. See the Stage 3 report.
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    EXPECT_EQ(pageGroupNamed(bar, MainWindow::CacheTabIndex, "Policy"), nullptr);

    // The in-tab control is untouched and still the only way there.
    auto *cacheTab = window.findChild<CacheTab *>();
    ASSERT_NE(cacheTab, nullptr);
    EXPECT_EQ(cacheTab->findChildren<CacheConfigWidget *>().size(), 3);
}

TEST(RibbonStage3, CompilerButtonsCallTheTabsOwnSlots)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *compilerTab = window.findChild<CompilerTab *>();
    ASSERT_NE(compilerTab, nullptr);

    auto *input  = compilerTab->findChild<QPlainTextEdit *>("inputTextEdit");
    auto *output = compilerTab->findChild<QPlainTextEdit *>("outputTextEdit");
    ASSERT_NE(input, nullptr);
    ASSERT_NE(output, nullptr);

    QToolButton *compile = buttonNamed(pageGroupNamed(bar, MainWindow::CompilerTabIndex, "Build"),
                                       QStringLiteral("Compile"));
    QToolButton *copy = buttonNamed(pageGroupNamed(bar, MainWindow::CompilerTabIndex, "Output"),
                                    QStringLiteral("Copy to Clipboard"));
    ASSERT_NE(compile, nullptr);
    ASSERT_NE(copy, nullptr);

    // Empty input is the one compile path that reaches no network: the tab
    // writes its own error into the output pane and returns. That is enough
    // to show the ribbon button really calls the tab's slot.
    input->setPlainText(QString());
    output->setPlainText(QString());
    compile->click();
    EXPECT_TRUE(output->toPlainText().contains(QStringLiteral("Input is empty")))
        << "the ribbon did not reach onConvertToAssemblyClicked(): '"
        << output->toPlainText().toStdString() << "'";

    // Copy takes whatever is in the output pane to the clipboard.
    output->setPlainText(QStringLiteral("addi x1, x0, 7"));
    QGuiApplication::clipboard()->setText(QStringLiteral("something else"));
    copy->click();
    EXPECT_EQ(QGuiApplication::clipboard()->text(), QStringLiteral("addi x1, x0, 7"))
        << "the ribbon did not reach onCopyToMainEditorClicked()";
}

TEST(RibbonStage3, CompileButtonFollowsTheTabsDisabledState)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    auto *compilerTab = window.findChild<CompilerTab *>();
    ASSERT_NE(compilerTab, nullptr);

    auto *inTab = compilerTab->findChild<QPushButton *>("convertToAssemblyButton");
    ASSERT_NE(inTab, nullptr);

    QToolButton *onRibbon = buttonNamed(pageGroupNamed(bar, MainWindow::CompilerTabIndex, "Build"),
                                        QStringLiteral("Compile"));
    ASSERT_NE(onRibbon, nullptr);

    // They start in step.
    EXPECT_TRUE(inTab->isEnabled());
    EXPECT_TRUE(onRibbon->isEnabled());
    EXPECT_TRUE(compilerTab->isCompileEnabled());

    // A compile in flight is exactly this: the tab disables its own button.
    // The ribbon must not stay clickable, or it would start a second one.
    inTab->setEnabled(false);
    EXPECT_FALSE(compilerTab->isCompileEnabled());
    EXPECT_FALSE(onRibbon->isEnabled()) << "the ribbon can still start a second compile";

    inTab->setEnabled(true);
    EXPECT_TRUE(onRibbon->isEnabled()) << "the ribbon button never came back";

    // Copy to Clipboard has no disabled state in the tab, so the ribbon
    // gives it none either rather than inventing one.
    auto *inTabCopy = compilerTab->findChild<QPushButton *>("copyToMainEditorButton");
    ASSERT_NE(inTabCopy, nullptr);
    EXPECT_TRUE(inTabCopy->isEnabled());
    QToolButton *copy = buttonNamed(pageGroupNamed(bar, MainWindow::CompilerTabIndex, "Output"),
                                    QStringLiteral("Copy to Clipboard"));
    ASSERT_NE(copy, nullptr);
    EXPECT_TRUE(copy->isEnabled());
}

TEST(RibbonStage3, ProfilerPageStaysEmpty)
{
    // Nothing in the profiler tab is a command, so its page carries only the
    // persistent groups. No placeholder: an empty page says "nothing here"
    // more honestly than a greyed-out button that never does anything.
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    RibbonPage *page = bar->page(MainWindow::ProfilerTabIndex);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(page->groupCount(), 0);
    EXPECT_TRUE(page->groups().isEmpty());

    // ...and the persistent groups are still reachable from it.
    EXPECT_EQ(bar->persistentGroupCount(), 2);
}

TEST(RibbonStage3, NothingOnTheRibbonAddedAShortcut)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    // Shared actions keep the shortcut they already had; nothing built for
    // the ribbon gets one. A second action with the same key sequence is
    // what produces "Ambiguous shortcut overload" at runtime, which the
    // smoke harness also watches for.
    // Everything the menus and the execution set already owned, submenus
    // included.
    QList<QAction *> existing = executionActions(bar);
    std::function<void(const QList<QAction *> &)> walk = [&](const QList<QAction *> &actions)
    {
        for (QAction *a : actions)
        {
            existing.append(a);
            if (QMenu *menu = a->menu())
                walk(menu->actions());
        }
    };
    walk(window.menuBar()->actions());

    for (QAction *a : ribbonActions(bar))
    {
        if (a->shortcut().isEmpty())
            continue;
        EXPECT_TRUE(existing.contains(a))
            << "'" << a->text().toStdString() << "' is a new ribbon action with a shortcut ("
            << a->shortcut().toString().toStdString() << ")";
    }

    // And the window as a whole still holds the same seven, all distinct.
    QStringList sequences;
    for (QAction *a : window.findChildren<QAction *>())
        if (!a->shortcut().isEmpty())
            sequences << a->shortcut().toString();
    EXPECT_EQ(sequences.size(), 7);
    QStringList distinct = sequences;
    distinct.removeDuplicates();
    EXPECT_EQ(distinct.size(), sequences.size()) << "two actions share a key sequence";
}

namespace
{
/// Every ribbon button, by the group it sits in, its label and its size.
/// Large means icon over text; small means icon beside text. A button with
/// no icon renders the same either way, so "large" is only meaningful where
/// there is an icon to put above the label - which is why Search is small.
struct ButtonSpec
{
    const char         *group;
    const char         *label;
    Qt::ToolButtonStyle style;
};
constexpr Qt::ToolButtonStyle kLarge = Qt::ToolButtonTextUnderIcon;
constexpr Qt::ToolButtonStyle kSmall = Qt::ToolButtonTextBesideIcon;

const QList<ButtonSpec> kPersistentButtons = {
    {"Run", "Run", kLarge},       {"Run", "Debug Run", kLarge}, {"Run", "Pause", kSmall},
    {"Run", "Step", kSmall},      {"Run", "Undo", kSmall},      {"Run", "Redo", kSmall},
    {"Run", "Processor", kSmall}, {"File", "Open", kLarge},     {"File", "Save", kLarge},
    {"File", "Problems", kSmall},
};

const QList<ButtonSpec> kPageButtons = {
    {"View", "Editor", kSmall},           {"View", "Expanded", kSmall},
    {"Find", "Search", kSmall},           {"Display", "Hex", kSmall},
    {"Display", "Binary", kSmall},        {"Display", "Decimal", kSmall},
    {"Datapath", "Wires Stay Active", kSmall},
    {"Registers", "Binary", kSmall},      {"Registers", "Hexadecimal", kSmall},
    {"Registers", "Decimal/Float", kSmall},
    {"Level", "Instruction", kSmall},     {"Level", "L1", kSmall},
    {"Level", "L2", kSmall},              {"Build", "Compile", kLarge},
    {"Output", "Copy to Clipboard", kSmall},
};
} // namespace

TEST(RibbonStage3, EveryButtonHasTheLabelAndSizeItShould)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    auto render = [](const QList<ButtonSpec> &specs)
    {
        QStringList out;
        for (const ButtonSpec &spec : specs)
            out << QStringLiteral("%1/%2/%3")
                       .arg(QString::fromUtf8(spec.group), QString::fromUtf8(spec.label),
                            spec.style == kLarge ? QStringLiteral("large")
                                                 : QStringLiteral("small"));
        return out;
    };

    QStringList persistent;
    QStringList onPages;
    auto collect = [](QStringList &into, RibbonGroup *group)
    {
        for (QToolButton *button : group->buttons())
            into << QStringLiteral("%1/%2/%3")
                        .arg(group->caption(), button->text(),
                             button->toolButtonStyle() == kLarge ? QStringLiteral("large")
                                                                 : QStringLiteral("small"));
    };
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        collect(persistent, bar->persistentGroup(i));
    for (int p = 0; p < bar->count(); ++p)
        for (RibbonGroup *group : bar->page(p)->groups())
            collect(onPages, group);

    EXPECT_EQ(persistent, render(kPersistentButtons));
    EXPECT_EQ(onPages, render(kPageButtons));
}

// ══════════════════ Stage 4A: retiring the old toolbar ════════════════════

TEST(RibbonStage4, ExecutionActionsOutliveTheToolbar)
{
    // The premise the retirement rested on: QWidget::addAction() does not
    // transfer ownership, so the QToolBar never owned these and deleting it
    // could not take one with it. Proved by doing it rather than by reading
    // the constructor.
    //
    // The window has no toolbar any more, so the test builds one, puts the
    // actions on it exactly as setUpToolBar() did, and deletes it. Without
    // that this would assert against an empty list and prove nothing.
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(window.findChildren<QToolBar *>().isEmpty())
        << "the old main toolbar is back";

    const QList<QAction *> actions = executionActions(bar);
    ASSERT_EQ(actions.size(), 7);

    // Every one is parented to the window, not to any widget showing it.
    for (QAction *a : actions)
        EXPECT_EQ(a->parent(), &window) << "'" << a->text().toStdString() << "' is owned by "
                                        << a->parent()->metaObject()->className();

    // Remember them through guards, so a dangling pointer is observable
    // without dereferencing freed memory.
    QList<QPointer<QAction>> guards;
    QStringList              labels;
    for (QAction *a : actions)
    {
        guards.append(QPointer<QAction>(a));
        labels << a->text();
    }

    {
        QToolBar *rebuilt = window.addToolBar("Rebuilt Toolbar");
        for (QAction *a : actions)
            rebuilt->addAction(a);
        rebuilt->addSeparator();
        EXPECT_EQ(window.findChildren<QToolBar *>().size(), 1);
        delete rebuilt;
    }
    EXPECT_TRUE(window.findChildren<QToolBar *>().isEmpty());

    for (int i = 0; i < guards.size(); ++i)
        EXPECT_FALSE(guards.at(i).isNull()) << "'" << labels.at(i).toStdString() << "' died with the toolbar";

    // ...and the theme re-tint pass, which walks findChildren<QAction*> on
    // the window, still reaches each of them.
    const QList<QAction *> stillChildren = window.findChildren<QAction *>();
    for (int i = 0; i < guards.size(); ++i)
    {
        if (guards.at(i).isNull())
            continue;
        EXPECT_TRUE(stillChildren.contains(guards.at(i).data()))
            << "'" << labels.at(i).toStdString() << "' is no longer found by the re-tint pass";
    }

    // The pass itself runs clean and leaves the tags in place.
    window.themeChangedSlot(QStringLiteral("kites-dark"));
    for (int i = 0; i < guards.size(); ++i)
        if (!guards.at(i).isNull() && labels.at(i) != QStringLiteral("Problems"))
            EXPECT_NE(themedIconOf(guards.at(i).data()), Icon::IconCount)
                << "'" << labels.at(i).toStdString() << "' lost its icon tag";

    // The ribbon buttons still work off those same actions afterwards.
    for (QToolButton *button : persistentGroupNamed(bar, "Run")->buttons())
        EXPECT_NE(button->defaultAction(), nullptr);
}

TEST(RibbonStage4, EveryToolbarCommandIsOnTheRibbonAsTheSameObject)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    // Named by role, in the Run group's order.
    const QList<QAction *> actions = executionActions(bar);
    ASSERT_EQ(actions.size(), 7);
    QStringList labels;
    for (QAction *a : actions)
        labels << a->text();
    EXPECT_EQ(labels, (QStringList{"Run", "Debug Run", "Pause", "Step", "Undo", "Redo",
                                   "Processor"}));

    // The speed control is a widget, not an action, and lives in the group.
    QSpinBox *spin = speedSpin(window);
    ASSERT_NE(spin, nullptr);
    EXPECT_TRUE(bar->isAncestorOf(spin));
    EXPECT_EQ(spin->minimum(), 1);
    EXPECT_EQ(spin->maximum(), 10000);

    // Open, Save and Problems are on the ribbon too and still in the menus.
    const QList<QAction *> onRibbon = ribbonActions(bar);
    QAction *open = menuEntry(window, 0, 0);
    QAction *save = menuEntry(window, 0, 1);
    QAction *problems = window.findChild<ProblemsDock *>()->toggleViewAction();
    ASSERT_NE(open, nullptr);
    ASSERT_NE(save, nullptr);
    EXPECT_TRUE(onRibbon.contains(open));
    EXPECT_TRUE(onRibbon.contains(save));
    EXPECT_TRUE(onRibbon.contains(problems));

    // No toolbar is left, which is what this stage removed.
    EXPECT_EQ(window.findChildren<QToolBar *>().size(), 0);

    // The menu bar is untouched: File, Settings, Help, View, in that order.
    QStringList menus;
    for (QAction *a : window.menuBar()->actions())
        menus << a->text();
    EXPECT_EQ(menus, (QStringList{"&File", "&Settings", "&Help", "&View"}));
}

// ════════════════ Stage 4B1: small buttons stack in columns ═══════════════
//
// Nothing here hard-codes a pixel count. The capacity and the cell height are
// measured from the style and the font at runtime, so these assertions hold
// at a different font size or device pixel ratio instead of pinning this
// machine's numbers.

namespace
{
/// The packing rule, written out independently of RibbonGroup: items are
/// filled down a column until it is full, then a new column starts, and a
/// separator or a large button ends the column in progress.
QList<int> expectedColumns(const QList<int> &runsBetweenSeparators, int capacity)
{
    QList<int> columns;
    for (int items : runsBetweenSeparators)
        while (items > 0)
        {
            const int take = std::min(items, capacity);
            columns << take;
            items -= take;
        }
    return columns;
}

RibbonGroup *anyGroupNamed(RibbonBar *bar, const QString &caption)
{
    if (RibbonGroup *g = persistentGroupNamed(bar, caption))
        return g;
    for (int p = 0; p < bar->count(); ++p)
        for (RibbonGroup *g : bar->page(p)->groups())
            if (g->caption() == caption)
                return g;
    return nullptr;
}
} // namespace

TEST(RibbonStage4B1, SmallButtonsStackIntoColumns)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    RibbonGroup *run = persistentGroupNamed(bar, "Run");
    ASSERT_NE(run, nullptr);
    const int capacity = run->rowCapacity();
    EXPECT_LE(capacity, 3);
    // Two is the design expectation, not a law of the layout: a 20 px icon
    // row fits twice inside a band sized for a 32 px icon under any sane
    // font. If a font ever makes this 1 the ribbon silently goes back to one
    // button per column and gets much wider, so it is worth being told.
    EXPECT_GE(capacity, 2) << "nothing is stacking; every small button is its own column";

    // Run has three separator-delimited runs of small items: Pause+Step,
    // Undo+Redo, Processor+the speed spin box. Each is packed on its own.
    EXPECT_EQ(run->columnSizes(), expectedColumns({2, 2, 2}, capacity));

    // Groups with no separators pack as one run.
    const QList<QPair<QString, int>> plain = {
        {"View", 2}, {"Find", 1}, {"Display", 3}, {"Datapath", 1},
        {"Registers", 3}, {"Level", 3}, {"Output", 1}, {"File", 1},
    };
    for (const auto &[caption, items] : plain)
    {
        RibbonGroup *group = anyGroupNamed(bar, caption);
        ASSERT_NE(group, nullptr) << caption.toStdString();
        EXPECT_EQ(group->columnSizes(), expectedColumns({items}, capacity))
            << "group '" << caption.toStdString() << "'";
    }

    // Build holds only the large Compile button, so it has no column at all.
    RibbonGroup *build = anyGroupNamed(bar, "Build");
    ASSERT_NE(build, nullptr);
    EXPECT_TRUE(build->columnSizes().isEmpty());

    // Stacking must actually have happened, or every assertion above is the
    // old one-per-column layout passing by accident.
    if (capacity > 1)
        EXPECT_LT(run->columnSizes().size(), 6)
            << "six columns means nothing stacked";
}

TEST(RibbonStage4B1, StackingAddsNoHeightToThePanel)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    // A group holding one large button and nothing else is the reference
    // band: whatever a large icon-over-text button needs, plus the caption
    // and the margins. If a column ever overflowed, a real group would come
    // out taller than this and the panel would grow with it.
    //
    // The reference button is given an explicit 32 px icon. kites_test does
    // not link the .qrc objects, so the window's real large buttons have
    // null icons and are shorter here than in the application - but the band
    // RibbonGroup derives its capacity from is measured with a synthetic
    // pixmap, so the reference has to carry one too or it would be comparing
    // against a different number than the production code used.
    QPixmap placeholder(32, 32);
    placeholder.fill(Qt::transparent);
    QAction reference("Reference", &window);
    reference.setIcon(QIcon(placeholder));

    RibbonGroup oneLarge("Reference");
    oneLarge.addLarge(&reference);
    const int band = oneLarge.sizeHint().height();
    EXPECT_GT(band, 0);

    int tallest = 0;
    auto check = [band, &tallest](RibbonGroup *group)
    {
        const int h = group->sizeHint().height();
        tallest = std::max(tallest, h);
        EXPECT_LE(h, band) << "group '" << group->caption().toStdString()
                           << "' is taller than the band a large button defines";
    };
    for (int i = 0; i < bar->persistentGroupCount(); ++i)
        check(bar->persistentGroup(i));
    for (int p = 0; p < bar->count(); ++p)
        for (RibbonGroup *group : bar->page(p)->groups())
            check(group);

    // The panel is exactly as tall as its tallest group and no taller than
    // the band. The application's own 97 px is asserted by the out-of-tree
    // screenshot harness, which links the resources and the stylesheet; this
    // is the part that holds whatever the font and the icons turn out to be.
    QWidget *panel = bar->findChild<QWidget *>("ribbonPanel");
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->sizeHint().height(), tallest);
    EXPECT_LE(panel->sizeHint().height(), band) << "stacking made the panel taller";
}

TEST(RibbonStage4B1, TheSpeedSpinBoxIsStillOneWidgetInTheRunGroup)
{
    MainWindow window;
    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    int count = 0;
    for (QSpinBox *candidate : window.findChildren<QSpinBox *>())
        if (candidate->suffix() == QStringLiteral(" ms"))
            ++count;
    EXPECT_EQ(count, 1) << "the spin box was copied, not moved into a column";

    QSpinBox *spin = speedSpin(window);
    ASSERT_NE(spin, nullptr);

    // Stacking reparents it into a column host, which is inside the Run
    // group, which is inside the ribbon. One parent, one owner, one object.
    RibbonGroup *run = persistentGroupNamed(bar, "Run");
    ASSERT_NE(run, nullptr);
    EXPECT_TRUE(run->isAncestorOf(spin)) << "the spin box left the Run group";
    EXPECT_TRUE(bar->isAncestorOf(spin));
    EXPECT_NE(spin->parentWidget(), nullptr);

    // Unchanged in every way that is not its position.
    EXPECT_EQ(spin->minimum(), 1);
    EXPECT_EQ(spin->maximum(), 10000);
    EXPECT_EQ(spin->value(), 1000);
    EXPECT_EQ(spin->suffix(), QStringLiteral(" ms"));
}

TEST(RibbonStage4B1, NoButtonIsClippedOnAnyPage)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    QApplication::processEvents();

    RibbonBar *bar = window.findChild<RibbonBar *>();
    ASSERT_NE(bar, nullptr);

    int examined = 0;
    auto check = [&examined](RibbonGroup *group)
    {
        const QRect bounds = group->rect();
        for (QToolButton *button : group->buttons())
        {
            if (!button->isVisible())
                continue;
            const QRect placed(button->mapTo(group, QPoint(0, 0)), button->size());
            EXPECT_TRUE(bounds.contains(placed))
                << "'" << button->text().toStdString() << "' in group '"
                << group->caption().toStdString() << "' sticks out of its group: button "
                << placed.x() << "," << placed.y() << " " << placed.width() << "x"
                << placed.height() << " group " << bounds.width() << "x" << bounds.height();
            // A button squeezed below what it asked to draw would clip its
            // icon or its text.
            EXPECT_GE(button->height(), button->minimumSizeHint().height())
                << "'" << button->text().toStdString() << "' is shorter than it can draw in";
            ++examined;
        }
    };

    for (int page = 0; page < bar->count(); ++page)
    {
        window.setCurrentTab(static_cast<MainWindow::TabIndex>(page));
        QApplication::processEvents();
        for (int i = 0; i < bar->persistentGroupCount(); ++i)
            check(bar->persistentGroup(i));
        for (RibbonGroup *group : bar->page(page)->groups())
            check(group);
    }
    EXPECT_GT(examined, 0) << "no buttons were examined";
}
