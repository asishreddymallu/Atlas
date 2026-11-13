#include "main_window.h"
#include "assembler/assembler.h"
#include "common/assembled_program.h"
#include "common/globals.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/compiler_tab/compiler_tab.h"
#include "ui/dialogs/processor_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/memory_tab/memory_tab.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"
#include "ui/theme/icon_manager.h"
#include "ui_main_window.h"
#include "utils/utils.h"
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTabBar>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <cassert>

namespace Kites
{
namespace
{
// Chrome icon size in logical pixels, shared by the toolbar and the ribbon's
// tab strip so the two never disagree. IconManager's default matches it.
constexpr int toolbarIconSize = 20;

// Icon size for the ribbon's large (icon-over-text) buttons. Actions shown
// both there and on the 20 px toolbar are rasterised at this size, so the
// toolbar scales one down rather than blowing a small one up.
constexpr QSize largeIconSize{32, 32};

// Keeps the shortcut hint and the tooltip in one place, so a tooltip reworded
// at runtime (Run<->Stop, Pause<->Resume) cannot lose its key hint.
void setActionHint(QAction *action, const QString &description)
{
    const QString keys = action->shortcut().toString(QKeySequence::NativeText);
    action->setToolTip(keys.isEmpty() ? description
                                      : QStringLiteral("%1  (%2)").arg(description, keys));
}

// One exclusive row of small checkable buttons, for a choice that a widget
// inside a tab already owns: the memory and register display bases and the
// cache level. Only the buttons are built here - the caller wires what each
// one forwards to and how the tab reports back - so none of the three holds
// any state of its own, and all three read the same way.
//
// No shortcuts: everything on the ribbon is reached by mouse or through the
// menu that already owns the command.
QList<QAction *> addExclusiveChoices(QObject *owner, RibbonGroup *group, const QStringList &labels)
{
    auto *actionGroup = new QActionGroup(owner);
    actionGroup->setExclusive(true);

    QList<QAction *> actions;
    for (const QString &label : labels)
    {
        auto *action = new QAction(label, owner);
        action->setCheckable(true);
        actionGroup->addAction(action);
        group->addSmall(action);
        actions.append(action);
    }
    return actions;
}

// Checks one of those buttons and lets the exclusive group uncheck the rest.
// An index the row does not have leaves the buttons alone rather than
// guessing, so a value the ribbon does not offer cannot show a wrong one.
void checkChoice(const QList<QAction *> &actions, int index)
{
    if (index >= 0 && index < actions.size())
        actions.at(index)->setChecked(true);
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
// ui(new Ui::MainWindow)
{
    // ui->setuoUi(this);
    setWindowTitle("Kites RISC-V Simulator");
    setWindowIcon(QIcon(":/icons/kite.png"));
    ThemeManager::getInstance(); // applies the persisted (or default) theme before any UI is built
    setupVmStateDirectory();

    // well run the vm in a separate thread to keep the ui responsive
    // do not touch these 4 line unless you really know what you are doing
    m_processorManager = new ProcessorManager();
    m_processorThread = new QThread(this);
    m_processorManager->moveToThread(m_processorThread);
    m_processorThread->start();

            
    m_registerContainer = new RegisterContainer(this, m_processorManager->getRegisterFile());
    QWidget *central = new QWidget(this);
    // The ribbon spans the window above the content, so the central widget
    // stacks vertically where it used to run left-to-right beside a sidebar.
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    m_stackedTabs = new QStackedWidget(this);
    m_ribbon = new RibbonBar(this);
    setUpStatusBar();
    setUpToolBar();
    setUpMenubar();
    setUpTabs();
    setUpRibbon();
    setUpProblemsDock();
    // After the dock, because the Problems toggle is the dock's own action.
    // Deliberately a separate pass rather than folding it into setUpRibbon()
    // and moving that call: nothing here creates a QAction, so the window's
    // action creation order is exactly what it was.
    setUpPersistentGroups();
    setUpEditorPage();
    setUpMemoryPage();
    setUpProcessorPage();

    // m_registerContainer = new RegisterContainer(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(m_stackedTabs);
    splitter->addWidget(m_registerContainer);
    splitter->widget(1)->setMaximumWidth(350);

    mainLayout->addWidget(m_ribbon, 0);
    mainLayout->addWidget(splitter, 1);

    setCentralWidget(central);
    resize(1200, 800);
    connect(this, &MainWindow::runProcessorSignal, m_processorManager, &ProcessorManager::runSlot);
    connect(this, &MainWindow::debugRunProcessorSignal, m_processorManager,
            &ProcessorManager::debugRunSlot);
    // well temporarily disable the toolbar buttons when vm is running
    connect(m_processorManager, &ProcessorManager::runFinishedSignal, this, &MainWindow::runFinishedSlot);
    connect(m_processorManager, &ProcessorManager::runErrorSignal, this,
            [this](const QString &errorMessage, int sourceLine)
            {
                // first we enable the toolbar buttons
                runFinishedSlot();
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->showRuntimeError(sourceLine, errorMessage);
                }
                QMessageBox::critical(this, "Runtime Error", errorMessage);
            });

    //TODO : instead of using lambda just connect the signal to the functions directly
    connect(m_processorManager, &ProcessorManager::updateDisassemblySignal, this,
            [this](const QString &disassemblyText)
            {
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->updateDisassemblyView(disassemblyText.toStdString());
                }
            });
    connect(m_processorManager, &ProcessorManager::updateEditorHighlightSignal, this,
            [this](const std::vector<std::pair<int,std::string>> &editorLines, 
            const std::vector<std::pair<int,std::string>> &disassemblyLines)
            {
                auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                if (editorTab)
                {
                    editorTab->highlightLines(editorLines, disassemblyLines);
                }
            });
    connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, 
            this, &MainWindow::themeChangedSlot);
}

void MainWindow::setUpProblemsDock()
{
    m_problemsDock = new ProblemsDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_problemsDock);
    m_problemsDock->hide(); // stays out of the way until a Run reports something

    QAction *toggleAction = m_problemsDock->toggleViewAction();
    m_problemsToggleAction = toggleAction;
    toggleAction->setText("Problems");
    toggleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    setActionHint(toggleAction, "Show or hide the Problems list");

    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(toggleAction);

    auto *editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    if (!editorTab)
        return;

    connect(editorTab, &EditorTab::diagnosticsChangedSignal, this,
            [this, editorTab](const QVector<Diagnostic> &diagnostics)
            {
                m_problemsDock->setDiagnostics(diagnostics, editorTab->sourceLineCount());
                // surface the list when there is something to see; never hide
                // it on its own, so a dock the user opened stays open
                if (!diagnostics.isEmpty())
                    m_problemsDock->show();
            });

    connect(m_problemsDock, &ProblemsDock::diagnosticActivated, this,
            [editorTab](const Diagnostic &diagnostic) { editorTab->goToDiagnostic(diagnostic); });
}

void MainWindow::setUpStatusBar()
{
    QStatusBar *statusBar = this->statusBar();
    QLabel *statusLabel = new QLabel("Ready", this);
    statusBar->addPermanentWidget(statusLabel);
}

void MainWindow::setUpToolBar()
{
    QToolBar *toolbar         = addToolBar("Main Toolbar");
    // Held as members so every place that needs them - the ribbon from the
    // next commit, and runFinishedSlot() below - can name the action instead
    // of hunting for it by its user-visible text in whichever QToolBar Qt
    // happens to return first. Creation order is unchanged.
    m_processorAction = new QAction("Processor", this);
    m_runAction       = new QAction("Run", this);
    m_pauseAction     = new QAction("Pause", this);
    m_debugAction     = new QAction("Debug Run", this);
    m_stepAction      = new QAction("Step", this);
    m_undoAction      = new QAction("Undo", this);
    m_redoAction      = new QAction("Redo", this);

    m_stepAction->setDisabled(true);
    m_pauseAction->setDisabled(true);
    m_undoAction->setDisabled(true);
    m_redoAction->setDisabled(true);

    // set toolbar icons (resources in :/icons)
    setThemedIcon(m_processorAction, Icon::Processor);
    setThemedIcon(m_runAction, Icon::Play, largeIconSize);
    setThemedIcon(m_pauseAction, Icon::Pause);
    setThemedIcon(m_undoAction, Icon::Undo);
    setThemedIcon(m_redoAction, Icon::Redo);
    setThemedIcon(m_stepAction, Icon::Step);
    setThemedIcon(m_debugAction, Icon::Debug, largeIconSize);
    // Keyboard shortcuts. None of these collide with QPlainTextEdit's built-in
    // bindings (Ctrl+C/X/V/Z/Y/A and friends) or with the keys Editor
    // intercepts for the completer popup (Enter/Return/Escape/Tab/Backtab).
    // Toolbar Undo/Redo are deliberately left unbound: they undo a VM step,
    // not a text edit, and Ctrl+Z in the editor must stay text undo.
    m_runAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_debugAction->setShortcut(QKeySequence(Qt::Key_F9));
    m_pauseAction->setShortcut(QKeySequence(Qt::Key_F6));
    m_stepAction->setShortcut(QKeySequence(Qt::Key_F10));

    // tooltips for accessibility
    setActionHint(m_processorAction, "Processor settings");
    setActionHint(m_runAction, "Run program");
    setActionHint(m_pauseAction, "Pause execution");
    setActionHint(m_debugAction, "Start debug run");
    setActionHint(m_stepAction, "Execute single step");
    setActionHint(m_undoAction, "Undo last VM step");
    setActionHint(m_redoAction, "Redo last VM step");
    // one icon size for the whole chrome; the sidebar uses the same value
    toolbar->setIconSize(QSize(toolbarIconSize, toolbarIconSize));
    // single fixed toolbar: the drag handle is just visual noise
    toolbar->setMovable(false);

    //------------Spinbox for setting execution speed----------------
    m_stepDelaySpin = new QSpinBox(this);
    m_stepDelaySpin->setRange(1, 10000);
    m_stepDelaySpin->setValue(1000);
    m_stepDelaySpin->setSuffix(" ms");
    m_stepDelaySpin->setToolTip("Set Execution Speed (in milliseconds)");
    //---------------------------------------------------------------

    // The spin box is a widget, not an action, so it can have exactly one
    // parent: it now lives in the ribbon's Run group (setUpPersistentGroups)
    // rather than in a QWidgetAction here. A second spin box would be the
    // duplicated state this work is meant to avoid. Its range, default and
    // connection are unchanged; only its home moved.
    toolbar->addAction(m_processorAction);   // processor settings
    toolbar->addSeparator();
    toolbar->addAction(m_runAction);         // start / stop
    toolbar->addAction(m_debugAction);
    toolbar->addSeparator();
    toolbar->addAction(m_pauseAction);       // transport
    toolbar->addAction(m_stepAction);
    toolbar->addSeparator();
    toolbar->addAction(m_undoAction);        // history
    toolbar->addAction(m_redoAction);

    connect(m_runAction, &QAction::triggered, this,
            [this]()
            {
                if (m_runAction->text() == "Run")
                {
                    m_debugAction->setDisabled(true);
                    m_processorAction->setDisabled(true);
                    m_pauseAction->setEnabled(true);
                    m_runAction->setText("Stop");
                    setThemedIcon(m_runAction, Icon::Stop, largeIconSize);
                    setActionHint(m_runAction, "Stop execution");
                    this->run();
                }
                else if (m_runAction->text() == "Stop")
                {
                    // emit stopSignal();
                    m_runAction->setText("Run");
                    setThemedIcon(m_runAction, Icon::Play, largeIconSize);
                    setActionHint(m_runAction, "Run program");
                    m_processorManager->stop();
                }
            });

    connect(m_debugAction, &QAction::triggered, this,
            [this]()
            {
                m_debugAction->setDisabled(true);
                m_processorAction->setDisabled(true);
                // debug run starts paused, so the toolbar starts in the paused state
                m_runAction->setText("Stop");
                setThemedIcon(m_runAction, Icon::Stop, largeIconSize);
                setActionHint(m_runAction, "Stop execution");
                m_pauseAction->setEnabled(true);
                m_pauseAction->setText("Resume");
                setThemedIcon(m_pauseAction, Icon::Resume);
                setActionHint(m_pauseAction, "Resume execution");
                m_stepAction->setEnabled(true);
                m_undoAction->setEnabled(true);
                m_redoAction->setEnabled(true);
                this->run(true);
            });

    connect(m_processorAction, &QAction::triggered, this, &MainWindow::processorChangeDialog);

    connect(m_stepDelaySpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { m_processorManager->setStepDelay(value); });

    connect(m_pauseAction, &QAction::triggered, this,
            [this]()
            {
                if (m_pauseAction->text() == "Pause")
                {
                    m_pauseAction->setText("Resume");
                    setThemedIcon(m_pauseAction, Icon::Resume);
                    setActionHint(m_pauseAction, "Resume execution");
                    m_stepAction->setEnabled(true);
                    m_undoAction->setEnabled(true);
                    m_redoAction->setEnabled(true);
                    m_processorManager->pause();
                }
                else
                {
                    m_pauseAction->setText("Pause");
                    setThemedIcon(m_pauseAction, Icon::Pause);
                    setActionHint(m_pauseAction, "Pause execution");
                    m_stepAction->setDisabled(true);
                    m_undoAction->setDisabled(true);
                    m_redoAction->setDisabled(true);
                    m_processorManager->resume();
                }
            });

    // we also connect the vm paused at breakpoint signal to change the pause button text
    connect(m_processorManager, &ProcessorManager::processorPausedAtBreakpointSignal, this,
            [this]()
            {
                m_pauseAction->setText("Resume");
                setThemedIcon(m_pauseAction, Icon::Resume);
                setActionHint(m_pauseAction, "Resume execution");
                m_stepAction->setEnabled(true);
                m_undoAction->setEnabled(true);
                m_redoAction->setEnabled(true);
            });

    connect(m_stepAction, &QAction::triggered, this, [this]() { m_processorManager->step(); });
    connect(m_undoAction, &QAction::triggered, this, [this]() { m_processorManager->undo(); });
    connect(m_redoAction, &QAction::triggered, this, [this]() { m_processorManager->redo(); });
}

void MainWindow::setUpRibbon()
{
    // Same six tabs, same order, same icons as the sidebar they replace. The
    // order has to agree with TabIndex and with setUpTabs()'s addWidget
    // sequence; the assertion below is what keeps the three honest.
    m_ribbon->addTab("Editor", Icon::Editor);
    m_ribbon->addTab("Memory", Icon::Memory);
    m_ribbon->addTab("Processor", Icon::Processor);
    m_ribbon->addTab("Cache", Icon::Cache);
    m_ribbon->addTab("Compiler", Icon::Compiler);
    m_ribbon->addTab("Profiler", Icon::Profiler);
    assert(m_ribbon->count() == static_cast<int>(toIndex(TabIndex::TabCount)) &&
           "ribbon tab count must match TabIndex");

    // One page per tab, each still empty: Stage 2 fills them. The pages are
    // created by addTab() above.

    // Connected after the tabs exist, so adding the first one cannot drive a
    // switch against a content stack that is not populated yet.
    connect(m_ribbon, &RibbonBar::currentChanged, this,
            [this](int index) { setCurrentTab(static_cast<TabIndex>(index)); });

    // Establish the invariant explicitly rather than relying on both widgets
    // happening to start at 0.
    setCurrentTab(TabIndex::EditorTabIndex);

    // The strip is the chrome's single tab stop; from there Tab moves into
    // the content.
    setTabOrder(m_ribbon->tabBar(), m_stackedTabs);
}

void MainWindow::setUpPersistentGroups()
{
    // Shown left of the per-tab page on every tab, so execution stays
    // reachable while looking at any of them. Every button is bound to the
    // QAction the menu or the toolbar already uses - QToolButton's default
    // action - so enabled, checked, text, icon and tooltip are the action's,
    // not a second copy, and no second shortcut is registered.
    RibbonGroup *run = m_ribbon->addPersistentGroup("Run");
    run->addLarge(m_runAction);
    run->addLarge(m_debugAction);
    run->addWidget(m_stepDelaySpin); // reparented out of the toolbar
    run->addSeparator();
    run->addSmall(m_pauseAction);
    run->addSmall(m_stepAction);
    run->addSeparator();
    run->addSmall(m_undoAction);
    run->addSmall(m_redoAction);
    run->addSeparator();
    run->addSmall(m_processorAction);

    RibbonGroup *file = m_ribbon->addPersistentGroup("File");
    file->addLarge(m_openAction);
    file->addLarge(m_saveAction);
    file->addSmall(m_problemsToggleAction);
}

void MainWindow::setUpEditorPage()
{
    auto *editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    if (!editorTab)
        return;

    RibbonPage  *page = m_ribbon->page(toIndex(TabIndex::EditorTabIndex));
    RibbonGroup *view = page->addGroup("View");

    // The in-tab segmented buttons stay exactly where they are and remain the
    // state. These two only offer the same choice from the ribbon: triggering
    // one calls the slot the button would have called, and the tab reports
    // back so they show which pane is current. Nothing here decides anything.
    m_editorViewAction   = new QAction("Editor", this);
    m_expandedViewAction = new QAction("Expanded", this);
    for (QAction *action : {m_editorViewAction, m_expandedViewAction})
        action->setCheckable(true);

    // Exclusive, like the autoExclusive pair in the tab: checking one
    // unchecks the other without either of them deciding anything.
    auto *group = new QActionGroup(this);
    group->setExclusive(true);
    group->addAction(m_editorViewAction);
    group->addAction(m_expandedViewAction);

    m_editorViewAction->setToolTip("Show the source you wrote");
    m_expandedViewAction->setToolTip("Show pseudo-instructions expanded");

    view->addSmall(m_editorViewAction);
    view->addSmall(m_expandedViewAction);

    // Ribbon -> tab. QAction::triggered fires only on a real activation, not
    // on setChecked(), so the mirror below cannot drive this back.
    connect(m_editorViewAction, &QAction::triggered, this,
            [editorTab] { editorTab->switchToEditorView(); });
    connect(m_expandedViewAction, &QAction::triggered, this,
            [editorTab] { editorTab->switchToExpandedView(); });

    // Tab -> ribbon, so changing the pane with the in-tab buttons moves these
    // too. The exclusive group unchecks the sibling.
    connect(editorTab, &EditorTab::viewChangedSignal, this,
            [this](bool expanded)
            { (expanded ? m_expandedViewAction : m_editorViewAction)->setChecked(true); });

    // A run pins the Expanded pane and disables the in-tab Editor button.
    // Without this the ribbon would be a way around that lock.
    connect(editorTab, &EditorTab::expandedLockChangedSignal, this,
            [this](bool locked) { m_editorViewAction->setEnabled(!locked); });

    // Start in step with whatever the tab already shows.
    (editorTab->isExpandedView() ? m_expandedViewAction : m_editorViewAction)->setChecked(true);
    m_editorViewAction->setEnabled(!editorTab->isExpandedLocked());
}

void MainWindow::setUpMemoryPage()
{
    auto *memoryTab = dynamic_cast<MemoryTab *>(m_tabs[TabIndex::MemoryTabIndex]);
    if (!memoryTab)
        return;

    RibbonPage *page = m_ribbon->page(toIndex(TabIndex::MemoryTabIndex));

    // The search box and its button stay in the tab and keep owning the
    // address. This presses that button, so the search runs through exactly
    // the path it always has - there is no second way to search.
    RibbonGroup *find = page->addGroup("Find");
    auto *searchAction = new QAction("Search", this);
    searchAction->setToolTip("Jump the table to the address in the tab's search box");
    find->addLarge(searchAction);
    connect(searchAction, &QAction::triggered, memoryTab,
            [memoryTab] { memoryTab->triggerSearch(); });

    // The combo box in the tab stays the state. These three move it, and it
    // reports back so they show which base is current.
    RibbonGroup           *display = page->addGroup("Display");
    const QList<Base>      bases {Base::Hexadecimal, Base::Binary, Base::Decimal};
    const QList<QAction *> choices =
        addExclusiveChoices(this, display, {"Hex", "Binary", "Decimal"});

    for (int i = 0; i < choices.size(); ++i)
    {
        const Base base = bases.at(i);
        // triggered fires only on a real activation, never on setChecked(),
        // so the mirror below cannot drive this back.
        connect(choices.at(i), &QAction::triggered, memoryTab,
                [memoryTab, base] { memoryTab->setDisplayBase(base); });
    }

    connect(memoryTab, &MemoryTab::displayBaseChangedSignal, this,
            [choices, bases](Base base) { checkChoice(choices, bases.indexOf(base)); });

    // Start in step with whatever the combo box already shows.
    checkChoice(choices, bases.indexOf(memoryTab->displayBase()));
}

void MainWindow::setUpProcessorPage()
{
    RibbonPage *page = m_ribbon->page(toIndex(TabIndex::ProcessorTabIndex));

    // The menu's own action, on a button. Not a copy: checked state, text
    // and the connection to ProcessorTab::setWiresStayActive are the menu
    // entry's, so the two can never disagree and no second shortcut exists.
    //
    // Processor... is deliberately not repeated here - it is already in the
    // persistent Run group, which is on screen on this tab too.
    RibbonGroup *datapath = page->addGroup("Datapath");
    datapath->addSmall(m_wiresStayActiveAction);

    // The register panel sits beside the tabs rather than inside one, but
    // its display base is a processor-level reading choice, so the ribbon
    // offers it here. Its combo box stays the state.
    RibbonGroup           *registers = page->addGroup("Registers");
    const QList<Base>      bases {Base::Binary, Base::Hexadecimal, Base::Decimal};
    const QList<QAction *> choices =
        addExclusiveChoices(this, registers, {"Binary", "Hexadecimal", "Decimal/Float"});

    for (int i = 0; i < choices.size(); ++i)
    {
        const Base base = bases.at(i);
        connect(choices.at(i), &QAction::triggered, m_registerContainer,
                [this, base] { m_registerContainer->setDisplayBase(base); });
    }

    connect(m_registerContainer, &RegisterContainer::displayBaseChangedSignal, this,
            [choices, bases](Base base) { checkChoice(choices, bases.indexOf(base)); });

    checkChoice(choices, bases.indexOf(m_registerContainer->displayBase()));
}

void MainWindow::setCurrentTab(TabIndex index)
{
    const int target = static_cast<int>(toIndex(index));
    if (target < 0 || target >= static_cast<int>(toIndex(TabIndex::TabCount)))
        return;

    {
        // Blocks only RibbonBar's own currentChanged, which is what would
        // re-enter this function. The bar's internal strip-to-page wiring is
        // a separate connection on its QTabBar and still runs, so the panel
        // never shows a page belonging to a different tab.
        QSignalBlocker blocker(m_ribbon);
        m_ribbon->setCurrentIndex(target);
    }
    m_stackedTabs->setCurrentIndex(target);
}

MainWindow::TabIndex MainWindow::currentTab() const
{
    return static_cast<TabIndex>(m_stackedTabs->currentIndex());
}

void MainWindow::setUpMenubar()
{
    QMenu *fileMenu     = menuBar()->addMenu("&File");
    QMenu *settingsMenu = menuBar()->addMenu("&Settings");
    QMenu *helpMenu     = menuBar()->addMenu("&Help");

    QMenu *preferencesMenu = new QMenu("Preferences", this);
    QMenu *processorMenu   = new QMenu("Processor", this);
    
    QAction *openAction  = new QAction("Open", this);
    QAction *saveAction  = new QAction("Save", this);
    QAction *exitAction  = new QAction("Exit", this);
    QAction *aboutAction = new QAction("About", this);
    setThemedIcon(openAction, Icon::Open, largeIconSize);
    setThemedIcon(saveAction, Icon::Save, largeIconSize);
    openAction->setShortcut(QKeySequence::Open);  // Ctrl+O
    saveAction->setShortcut(QKeySequence::Save);  // Ctrl+S
    setActionHint(openAction, "Open an assembly file");
    setActionHint(saveAction, "Save the current program");
    m_saveAction = saveAction;
    m_openAction = openAction;
    setThemedIcon(aboutAction, Icon::About);

    ///////////File Menu///////////////////
    fileMenu->addAction(openAction);
    fileMenu->addAction(saveAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    ////////////Setting Menu////////////////
    settingsMenu->addMenu(preferencesMenu);
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    for (const ThemeData &theme : ThemeManager::getInstance().availableThemes())
    {
        QAction *themeAction = new QAction(theme.name, this);
        themeAction->setCheckable(true);
        themeAction->setChecked(theme.id == ThemeManager::getInstance().currentThemeId());
        themeGroup->addAction(themeAction);
        preferencesMenu->addAction(themeAction);
        connect(themeAction, &QAction::triggered, this,
                [this, id = theme.id]() { toggleTheme(id); });
    }
    //----------------------------------------------
    settingsMenu->addMenu(processorMenu);
    QAction *wireStayActiveAction = new QAction("Wires Stay Active", this);
    wireStayActiveAction->setCheckable(true);
    wireStayActiveAction->setChecked(false);
    m_wiresStayActiveAction = wireStayActiveAction;
    processorMenu->addAction(wireStayActiveAction);
    connect(wireStayActiveAction, &QAction::toggled, this,
            [this](bool checked)
            {
                auto processorTab =
                    dynamic_cast<ProcessorTab *>(m_tabs[TabIndex::ProcessorTabIndex]);
                if (processorTab)
                {
                    processorTab->setWiresStayActive(checked);
                }
            });
    //--------------------------------------------------
    QAction *advancedSettingsAction =
        new QAction("Advanced Settings", this);
    setThemedIcon(advancedSettingsAction, Icon::Settings);
    connect(advancedSettingsAction, &QAction::triggered, this,
            [this]()
            {
                SettingsDialog dialog(this);
                dialog.setWindowTitle("Advanced Processor Settings");
                dialog.exec();
            });
    settingsMenu->addAction(advancedSettingsAction);
    ///////////Help Menu///////////////////
    helpMenu->addAction(aboutAction);

    connect(openAction, &QAction::triggered, this,
            [this]()
            {
                QString filename = QFileDialog::getOpenFileName(
                    this, "Open File", "", "Assembly Files (*.asm *.s);;All Files (*)");

                if (!filename.isEmpty())
                {
                    QFile file(filename);
                    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                    {
                        QTextStream in(&file);
                        QString content = in.readAll();
                        auto editorTab =
                            dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                        if (editorTab)
                        {
                            editorTab->setRawText(content);
                        }
                        file.close();
                    }
                }
            });

    connect(saveAction, &QAction::triggered, this,
            [this]()
            {
                QString filename = QFileDialog::getSaveFileName(
                    this, "Save File", "", "Assembly Files (*.asm *.s);;All Files (*)");

                if (!filename.isEmpty())
                {
                    QFile file(filename);
                    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
                    {
                        QTextStream out(&file);
                        auto editorTab =
                            dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                        if (editorTab)
                        {
                            out << editorTab->getRawText().c_str();
                        }
                        file.close();
                    }
                }
            });
}

void MainWindow::setUpTabs()
{
    m_tabs[TabIndex::EditorTabIndex]    = new EditorTab(this);
    m_tabs[TabIndex::MemoryTabIndex]    = new MemoryTab(this, m_processorManager->getMemoryController());
    m_tabs[TabIndex::ProcessorTabIndex] = new ProcessorTab(this, m_processorManager);                      
    m_tabs[TabIndex::CacheTabIndex]     = new CacheTab(this, m_processorManager->getMemoryController());
    m_tabs[TabIndex::CompilerTabIndex]  = new CompilerTab(this);
    m_tabs[TabIndex::ProfilerTabIndex]  = new ProfilerTab(this, m_processorManager->getProfiler());
    // a little experiment
    connect(this, &MainWindow::processorChangedSignal,
            dynamic_cast<ProcessorTab *>(m_tabs[TabIndex::ProcessorTabIndex]),
            &ProcessorTab::onVMChanged);

    m_stackedTabs->addWidget(m_tabs[TabIndex::EditorTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::MemoryTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::ProcessorTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::CacheTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::CompilerTabIndex]);
    m_stackedTabs->addWidget(m_tabs[TabIndex::ProfilerTabIndex]);
}

bool MainWindow::tryParseAndLoadProgram()
{
    auto* editor = dynamic_cast<EditorTab*>(m_tabs[TabIndex::EditorTabIndex]);
    auto* profilerTab = dynamic_cast<ProfilerTab*>(m_tabs[TabIndex::ProfilerTabIndex]);

    if(!editor || !profilerTab) 
    {
        QMessageBox::critical(this, "Error", "Editor or Profiler tab not found.");
        return false;
    }
    editor->switchToExpandedView(); // switch to the expanded view 
                                    //to show expanded pseudoinstructions
    editor->resetErrorLines();    // we reset previous error lines
    editor->setCanWrite(false);   // we disable writing in editor while vm is running
    if (m_saveAction)
        m_saveAction->setEnabled(false); // read-only editor: a disabled QAction also
                                         // swallows its Ctrl+S shortcut
    m_processorManager->reset();
    try
    {
        std::string rawText = editor->getRawText();
        // The assembler runs on the expanded text, so every line and column it
        // reports is an expanded one. Keep the mapping so the editor can point
        // diagnostics back at the line the user actually wrote. With no custom
        // pseudo-instructions defined the mapping is the identity.
        std::vector<int> expandedToOriginalLine;
        rawText = customPseudoManager::expandPseudoInstruction(rawText, expandedToOriginalLine);
        editor->setExpansionMapping(expandedToOriginalLine);
        std::ofstream out(globals::temporary_assembly_file_path);
        out << rawText;
        out.close();
        AssembledProgram assembledProgram = assemble(globals::temporary_assembly_file_path.string());
        DumpDisasssembly(globals::disassembly_file_path, assembledProgram);
        std::ifstream in(globals::disassembly_file_path);
        std::stringstream buffer;
        buffer << in.rdbuf();
        editor->updateDisassemblyView(buffer.str());
        profilerTab->setSourceText(QString::fromStdString(rawText));
        
        m_processorManager->loadProgram(assembledProgram);
        m_processorManager->setBreakpoints(editor->getBreakpoints());
        return true;
    }
    catch (const std::exception &e)
    {
        editor->setErrorLinesFromFile(globals::errors_dump_file_path);
        QMessageBox::critical(this, "Error", e.what());
        return false;
    }
}

void MainWindow::run(bool startPaused)
{
    // as soon s as run is clicked we disable the toolbar buttons
    // disableToolBarButtons();
    if (tryParseAndLoadProgram())
    {

        auto editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
        if (editorTab)
        {
            editorTab->setExpandedLocked(true);
        }

        if (startPaused)
        {
            emit debugRunProcessorSignal();
        }
        else
        {
            emit runProcessorSignal();
        }
    }
    else
    {
        // if parsing or loading failed we re-enable the buttons
        // otherwise if the vm starts running the buttons will be re-enabled when vm finishes
        runFinishedSlot();
    }
}
void MainWindow::processorChangeDialog()
{
    ProcessorDialog dialog(this, m_processorManager->getProcessorType());
    dialog.setWindowTitle("Choose Processor");
    connect(&dialog, &ProcessorDialog::vmSelected, this, &MainWindow::processorChanged);
    dialog.exec();
}

void MainWindow::processorChanged(const ProcessorType &vmType)
{
    m_processorManager->changeProcessor(vmType);
    m_registerContainer->setRegisterFile(m_processorManager->getRegisterFile());
    auto *memtab = dynamic_cast<MemoryTab *>(m_tabs[TabIndex::MemoryTabIndex]);
    if (memtab)
    {
        memtab->changeMemoryController(m_processorManager->getMemoryController());
    }
    auto *cacheTab = dynamic_cast<CacheTab *>(m_tabs[TabIndex::CacheTabIndex]);
    if (cacheTab)
    {
        cacheTab->changeMemoryController(m_processorManager->getMemoryController());
    }
    emit processorChangedSignal();

    // well also have to change the processor design from here later
}

void MainWindow::runFinishedSlot()
{
    auto *editor = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    editor->setCanWrite(true); // re-enable writing in editor when vm stops
    if (m_saveAction)
        m_saveAction->setEnabled(true);
    editor->setExpandedLocked(false);
    editor->clearHighlights(); // we clear any highlights when processor stops
    // other we are not alble to move the cursor as the paint
    //  keeps jumping to last highlighted line
    editor->switchToEditorView();

    // Same transitions as before, addressed by member instead of recovered
    // from findChildren<QToolBar*>()[0] and matched on user-visible text.
    //
    // Where the old loop asked "is this the one that currently reads Stop?",
    // this asks which icon the action is wearing. setThemedIcon() sets the
    // icon in lockstep with the text at every site that flips either one, so
    // the two agree in every reachable state - and the icon tag is a stable
    // property rather than a label a reword or a translation would break.
    m_processorAction->setEnabled(true);
    m_debugAction->setEnabled(true);
    m_runAction->setEnabled(true);

    if (themedIconOf(m_runAction) == Icon::Stop)
    {
        m_runAction->setText("Run");
        setThemedIcon(m_runAction, Icon::Play, largeIconSize);
        m_runAction->setToolTip("Run program");
        // when the program finishes we set the run button text back to run
    }
    if (themedIconOf(m_pauseAction) == Icon::Resume)
    {
        m_pauseAction->setText("Pause");
        setThemedIcon(m_pauseAction, Icon::Pause);
        m_pauseAction->setToolTip("Pause execution");
        // in case we stop while paused
    }

    m_pauseAction->setDisabled(true);
    m_stepAction->setDisabled(true);
    // Undo and Redo are deliberately left as they are: the old loop matched
    // no case for them either, so a debug run that ends leaves VM history
    // navigable exactly as it did before.
}


void MainWindow::toggleTheme(const QString &themeId)
{
    ThemeManager::getInstance().setTheme(themeId);
}

void MainWindow::themeChangedSlot([[maybe_unused]] const QString &themeId)
{
    // Icons are flat SVGs tinted with the theme's icon colour, so every one of
    // them has to be re-tinted when the theme changes. Each action was tagged
    // with its Icon by setThemedIcon(), so this is a single pass over every
    // action in the window - toolbar, menus and submenus alike - with no
    // dependence on user-visible text.
    for (QAction *action : this->findChildren<QAction *>())
    {
        const Icon icon = themedIconOf(action);
        if (icon != Icon::IconCount)
            setThemedIcon(action, icon, themedIconSizeOf(action));
    }

    // tab-strip entries are QIcons on a QTabBar, not actions, so the pass
    // above does not reach them; the bar re-reads IconManager itself.
    m_ribbon->retintIcons();
}

MainWindow::~MainWindow()
{
    m_processorManager->stop();
    if(m_processorThread->isRunning())
    {
        m_processorThread->quit();
        m_processorThread->wait();
    }
    // delete ui;
}
} // namespace Kites
