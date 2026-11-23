#include "ui/timing_grid/timing_grid.h"
#include "ui/timing_grid/timing_overlay.h"
#include "main_window.h"
#include "assembler/assembler.h"
#include "common/assembled_program.h"
#include "common/globals.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "processor/processor_choice.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cache_tab.h"
#include "cache_lab/lab_presets.h"
#include "ui/compiler_tab/compiler_tab.h"
#include "scheduler_gate/reorder_gate.h"
#include "ui/dialogs/about_dialog.h"
#include "ui/dialogs/processor_dialog.h"
#include "ui/dialogs/reorder_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/memory_tab/memory_tab.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/output_panel/output_dock.h"
#include "ui/profiler_tab/profiler_tab.h"
#include "ui/ribbon/ribbon_group.h"
#include "ui/ribbon/ribbon_page.h"
#include "ui/theme/icon_manager.h"
#include "ui_main_window.h"
#include "utils/utils.h"
#include <QActionGroup>
#include <QThread>
#include <QApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QTabBar>
#include <QVBoxLayout>
#include <QSplitter>
#include <QMessageBox>
#include <cassert>

namespace Atlas
{
namespace
{
// An action shown both on a large ribbon button and on a small one is
// rasterised at the large size, so the small site scales one down rather
// than blowing a small one up. The sizes themselves live in icon_manager.h.
constexpr QSize largeIconSize{largeRibbonIconSize, largeRibbonIconSize};

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
        actions.append(action);
    }
    // One segmented run rather than loose buttons in the column grid. None
    // of these has an icon, and three separate text buttons do not read as
    // one choice; this matches the pair in the editor's Input header.
    group->addSegmented(actions);
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
    setWindowTitle("Atlas RISC-V Simulator");
    setWindowIcon(QIcon(":/icons/atlas.png"));
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
    setUpExecutionActions();
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
    setUpCachePage();
    setUpCompilerPage();

    // m_registerContainer = new RegisterContainer(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(m_stackedTabs);
    splitter->addWidget(m_registerContainer);
    splitter->widget(1)->setMaximumWidth(350);

    mainLayout->addWidget(m_ribbon, 0);
    mainLayout->addWidget(splitter, 1);

    setCentralWidget(central);
    m_timingOverlay->setHost(central, m_ribbon);
    resize(1200, 800);
    // requestRun/requestDebugRun snapshot the stop epoch on THIS thread
    // and post it with the invocation, so a Stop issued afterwards
    // cancels the run even if it is already queued or already dequeued.
    connect(this, &MainWindow::runProcessorSignal, m_processorManager,
            &ProcessorManager::requestRun, Qt::DirectConnection);
    connect(this, &MainWindow::debugRunProcessorSignal, m_processorManager,
            &ProcessorManager::requestDebugRun, Qt::DirectConnection);
    // well temporarily disable the execution buttons when vm is running
    connect(m_processorManager, &ProcessorManager::runFinishedSignal, this, &MainWindow::runFinishedSlot);
    connect(m_processorManager, &ProcessorManager::runErrorSignal, this,
            [this](const QString &errorMessage, int sourceLine)
            {
                // first we enable the execution buttons
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

    // Editor cursor -> the stages holding that line -> the circuit view.
    // The manager also re-emits on every clock, and processorChanged()
    // pushes the selection into a replaced scene. The second connection is
    // auto: during a run the manager emits on the worker thread, so it is
    // queued to this one.
    if (auto *editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]))
    {
        // Direct: the manager lives on the processor thread
        // (moveToThread above), and the selection reads the program's line
        // mapping, which loadProgram rewrites on this, the UI thread.
        connect(editorTab, &EditorTab::cursorLineChangedSignal, m_processorManager,
                &ProcessorManager::setSelectedSourceLine, Qt::DirectConnection);
    }
    connect(m_processorManager, &ProcessorManager::selectedStagesSignal, this,
            [this](quint8)
            {
                // The manager's CURRENT selection, not the value carried: a
                // queued emit can arrive late - stopping a paused run makes
                // the old processor clock once more on the worker thread,
                // and that selection would otherwise land on the scene of
                // the processor that replaced it.
                if (CircuitScene *scene = m_processorManager->getCircuitScene())
                    scene->setSelectedStages(m_processorManager->selectedStages());
            });
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
    setThemedIcon(toggleAction, Icon::Problems);

    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction(toggleAction);

    // The Output panel (known_issues 140): what the program prints, and the
    // line an ecall read asks for. Beside Problems in the bottom area.
    m_outputDock = new OutputDock(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_outputDock);
    tabifyDockWidget(m_problemsDock, m_outputDock);
    m_outputDock->hide(); // shown when the program prints or reads
    QAction *outputToggle = m_outputDock->toggleViewAction();
    outputToggle->setText("Output");
    setActionHint(outputToggle, "Show or hide the program's output");
    viewMenu->addAction(outputToggle);

    // WP-03: the pipeline timing grid. T6: laid over the bottom of the
    // central area rather than docked, so opening it moves nothing else (a
    // dock took its height from the editor, the circuit and the registers).
    // Hidden until asked for; placed over the central widget once that
    // exists (setHost, in the constructor).
    //
    // The toggle is created first and owned by the window, where the dock's
    // toggleViewAction sat in the window's object tree, and the overlay is a
    // direct child of the window like the dock was: harnesses list the
    // window's actions and spin boxes in tree order.
    m_timingToggleAction = new QAction("Pipeline timing", this);
    m_timingToggleAction->setCheckable(true);
    m_timingPanel = new TimingGridPanel();
    m_timingPanel->setUp(m_processorManager, m_stepAction, m_undoAction, m_redoAction);
    m_timingOverlay = new TimingOverlay("Pipeline timing", m_timingPanel, this);
    m_timingOverlay->setObjectName("timingDock");
    m_timingOverlay->hide();
    connect(m_timingToggleAction, &QAction::toggled, m_timingOverlay, &QWidget::setVisible);
    connect(m_timingOverlay, &TimingOverlay::visibilityChanged, m_timingToggleAction, &QAction::setChecked);
    setActionHint(m_timingToggleAction, "Show or hide the pipeline timing grid");
    viewMenu->addAction(m_timingToggleAction);
    // Queued: the processor emits these on the worker thread during a Run.
    connect(m_processorManager, &ProcessorManager::programOutputSignal, m_outputDock,
            [this](const QString &text)
            {
                showOutputDock();
                m_outputDock->appendOutput(text);
            },
            Qt::QueuedConnection);
    connect(m_processorManager, &ProcessorManager::inputRequestedSignal, m_outputDock,
            [this]()
            {
                showOutputDock();
                m_outputDock->requestInput();
            },
            Qt::QueuedConnection);
    connect(m_outputDock, &OutputDock::inputSubmitted, this,
            [this](const QString &line)
            {
                m_processorManager->pushInput(line.toStdString());
                // A Step that would have waited for this input runs now.
                if (m_stepAwaitingInput)
                {
                    m_stepAwaitingInput = false;
                    m_processorManager->step();
                }
            });

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

    // Added before "Ready" so that it sits to its left and "Ready" stays
    // where it has always been, hard against the right edge.
    m_processorStatusLabel = new QLabel(this);
    m_processorStatusLabel->setObjectName("processorStatusLabel");
    statusBar->addPermanentWidget(m_processorStatusLabel);
    updateProcessorStatusLabel();

    // What the program is doing: Ready, Running, Debugging, Program
    // finished, Stopped. Was a fixed "Ready" that never changed.
    m_runStatusLabel = new QLabel("Ready", this);
    m_runStatusLabel->setObjectName("runStatusLabel");
    statusBar->addPermanentWidget(m_runStatusLabel);
}

void MainWindow::showOutputDock()
{
    if (!m_outputDock)
        return;
    const bool firstTime = m_outputDock->isHidden() && !m_outputDockSized;
    m_outputDock->show();
    m_outputDock->raise();
    if (firstTime)
    {
        // Left to itself the dock takes half the window, and the ribbon
        // drops its labels for want of height (offscreen shot, U6). A few
        // lines of output and the input line are enough to start with.
        resizeDocks({m_outputDock}, {170}, Qt::Vertical);
        m_outputDockSized = true;
    }
}

void MainWindow::setRunStatus(const QString &text)
{
    if (m_runStatusLabel)
        m_runStatusLabel->setText(text);
}

void MainWindow::updateProcessorStatusLabel()
{
    if (!m_processorStatusLabel || !m_processorManager)
    {
        return;
    }
    const ProcessorType type = m_processorManager->getProcessorType();
    m_processorStatusLabel->setText(QStringLiteral("Processor: ") +
                                    QString::fromUtf8(processorDescription(type)));
    // The chooser's note for the same model, so the two always agree.
    const ProcessorChoice choice = processorChoiceFor(type);
    m_processorStatusLabel->setToolTip(
        QString::fromUtf8(processorNote(choice.pipeline, choice.hazard, choice.forwarding)));
}

void MainWindow::setUpExecutionActions()
{
    // These were the old main toolbar's. The toolbar is gone - the ribbon's
    // persistent Run group shows them now - but they are created here, in
    // this order, with the window as their parent, exactly as before. Every
    // shortcut, tooltip, icon tag and connection below is untouched; only
    // the widget that used to display them has been removed.
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

    // icons (resources in :/icons)
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
    // Undo/Redo are deliberately left unbound: they undo a VM step,
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
    //------------Spinbox for setting execution speed----------------
    m_stepDelaySpin = new QSpinBox(this);
    m_stepDelaySpin->setRange(1, 10000);
    m_stepDelaySpin->setValue(1000);
    m_stepDelaySpin->setSuffix(" ms");
    m_stepDelaySpin->setToolTip("Set Execution Speed (in milliseconds)");
    //---------------------------------------------------------------

    // The spin box is a widget, not an action, so it can have exactly one
    // parent: it lives in the ribbon's Run group (setUpPersistentGroups).
    // Its range, default and connection are unchanged; only its home moved.
    //
    // The actions are not added to anything here either. setUpPersistentGroups()
    // puts each on a ribbon button through QToolButton::setDefaultAction(),
    // which is what showed them on the toolbar too - one object, one state.

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
                    m_userStopped = false;
                    // A finished program offers Undo/Redo (runFinishedSlot);
                    // a running one must not: Undo on this thread under the
                    // running worker is known_issues 62's collision.
                    m_undoAction->setDisabled(true);
                    m_redoAction->setDisabled(true);
                    setRunStatus("Running");
                    this->run();
                }
                else if (m_runAction->text() == "Stop")
                {
                    // emit stopSignal();
                    m_runAction->setText("Run");
                    setThemedIcon(m_runAction, Icon::Play, largeIconSize);
                    setActionHint(m_runAction, "Run program");
                    m_userStopped = true;
                    if (!m_processorManager->stop())
                    {
                        QMessageBox::critical(this, "Error",
                                              "The processor did not stop within the timeout.");
                    }
                }
            });

    connect(m_debugAction, &QAction::triggered, this,
            [this]()
            {
                m_debugAction->setDisabled(true);
                m_processorAction->setDisabled(true);
                // debug run starts paused, so the controls start paused
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
                m_userStopped = false;
                setRunStatus("Debugging");
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

    connect(m_stepAction, &QAction::triggered, this,
            [this]()
            {
                if (m_stepAwaitingInput)
                    return; // the step is already waiting for the input line
                // A manual step runs on the UI thread. A step that serves an
                // ecall read with no input queued would block it in the
                // read's wait, and nobody could type (known_issues 144). Ask
                // first; the step runs when the line is entered.
                if (m_processorManager->nextStepWaitsForInput() && m_outputDock)
                {
                    m_stepAwaitingInput = true;
                    showOutputDock();
                    m_outputDock->requestInput();
                    return;
                }
                m_processorManager->step();
            });
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
    // QAction the menus already use - QToolButton's default
    // action - so enabled, checked, text, icon and tooltip are the action's,
    // not a second copy, and no second shortcut is registered.
    RibbonGroup *run = m_ribbon->addPersistentGroup("Run");
    run->addLarge(m_runAction);
    run->addLarge(m_debugAction);
    run->addSeparator();
    // Small items stack in columns, and a separator starts a new one, so the
    // three pairs below read as three blocks: transport, history, settings.
    run->addSmall(m_pauseAction);
    run->addSmall(m_stepAction);
    run->addSeparator();
    run->addSmall(m_undoAction);
    run->addSmall(m_redoAction);
    run->addSeparator();
    run->addSmall(m_processorAction);
    // Under Processor rather than after Debug Run, so it shares that column
    // instead of standing alone. Still the one spin box with one parent; only
    // which column holds it changed, and buttons() order is untouched.
    run->addWidget(m_stepDelaySpin); // its only home since the toolbar went

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

    // The same segmented treatment as the other exclusive, icon-less
    // choices, and the same shape as the pair this one mirrors in the tab.
    view->addSegmented({m_editorViewAction, m_expandedViewAction});

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

    // The File menu's own action, created in setUpMenubar().
    RibbonGroup *schedule = page->addGroup("Schedule");
    schedule->addLarge(m_suggestReorderAction);
}

void MainWindow::showReorderSuggestion()
{
    auto *editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
    if (!editorTab)
        return;

    const std::string   source = editorTab->getRawText();
    const ProcessorType type   = m_processorManager->getProcessorType();

    // Runs the original and the suggestion headless on a processor of its
    // own, capped at kReorderCycleCap cycles each; the loaded one is untouched.
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const ReorderSuggestion suggestion = Atlas::suggestReordering(source, type);
    QApplication::restoreOverrideCursor();

    if (!suggestion.offered())
    {
        QMessageBox box(this);
        box.setWindowTitle("Suggest reordering");
        box.setIcon(QMessageBox::Information);
        box.setText("No safe improvement found");
        box.setInformativeText(QString::fromStdString(reorderStatusReason(suggestion.status)));
        box.exec();
        return;
    }

    ReorderDialog dialog(QString::fromStdString(source),
                         QString::fromStdString(suggestion.suggestedSource),
                         QString::fromLatin1(processorDescription(type)),
                         suggestion.originalCycles, suggestion.suggestedCycles,
                         editorTab->isWritable(), this);
    if (dialog.exec() == QDialog::Accepted)
        editorTab->replaceRawTextAsOneEdit(dialog.suggestedSource());
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
    setThemedIcon(searchAction, Icon::Search);
    searchAction->setToolTip("Jump the table to the address in the tab's search box");
    // Small, not large: no icon in the set says "search", and a large button
    // without an icon is just a wider text button - the icon over the label
    // is the whole difference between the two sizes.
    find->addSmall(searchAction);
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

void MainWindow::setUpCachePage()
{
    auto *cacheTab = dynamic_cast<CacheTab *>(m_tabs[TabIndex::CacheTabIndex]);
    if (!cacheTab)
        return;

    RibbonPage *page = m_ribbon->page(toIndex(TabIndex::CacheTabIndex));

    // The inner tab widget stays the state. These three move it, and it
    // reports back, so the two always show the same level.
    RibbonGroup *level = page->addGroup("Level");
    const QList<CacheTab::CacheLevel> levels {CacheTab::Instruction, CacheTab::L1, CacheTab::L2};
    const QList<QAction *>            choices =
        addExclusiveChoices(this, level, {"Instruction", "L1", "L2"});

    for (int i = 0; i < choices.size(); ++i)
    {
        const CacheTab::CacheLevel target = levels.at(i);
        connect(choices.at(i), &QAction::triggered, cacheTab,
                [cacheTab, target] { cacheTab->setCacheLevel(target); });
    }

    connect(cacheTab, &CacheTab::cacheLevelChangedSignal, this,
            [choices, levels](CacheTab::CacheLevel changed)
            { checkChoice(choices, levels.indexOf(changed)); });

    checkChoice(choices, levels.indexOf(cacheTab->currentCacheLevel()));

    // No Policy group. "Custom policy..." cannot be offered here faithfully:
    // CacheConfigWidget::onCustomPolicyClicked() is only ever reached from
    // the replacement-policy combo having *already* moved to Custom, and it
    // relies on that. Called while another policy is selected it would load
    // the script into the cache while the combo still reads the old policy,
    // and the next unrelated config edit would silently reconfigure the
    // cache back. Cancelling is worse: the handler forces the combo to LRU
    // without emitting configChangedSignal, so the widget would claim LRU
    // while the cache kept running whatever was selected before. Reported
    // rather than wired; see the Stage 3 report.
}

void MainWindow::setUpCompilerPage()
{
    auto *compilerTab = dynamic_cast<CompilerTab *>(m_tabs[TabIndex::CompilerTabIndex]);
    if (!compilerTab)
        return;

    RibbonPage *page = m_ribbon->page(toIndex(TabIndex::CompilerTabIndex));

    RibbonGroup *build = page->addGroup("Build");
    auto        *compileAction = new QAction("Compile", this);
    // The compiler tab's own icon: this button *is* the compile command, so
    // it is the same idea, not a tab icon borrowed for something else.
    setThemedIcon(compileAction, Icon::Compiler, largeIconSize);
    compileAction->setToolTip("Compile the C/C++ in this tab to assembly");
    build->addLarge(compileAction);
    connect(compileAction, &QAction::triggered, compilerTab,
            [compilerTab] { compilerTab->onConvertToAssemblyClicked(); });

    // The tab disables its Compile button for the length of a compile. The
    // ribbon has to follow it, or it would be a way to start a second one.
    compileAction->setEnabled(compilerTab->isCompileEnabled());
    connect(compilerTab, &CompilerTab::compileEnabledChangedSignal, compileAction,
            &QAction::setEnabled);

    // The label is the one the in-tab button carries, because that is what
    // the command does: it copies to the clipboard, not to the editor.
    RibbonGroup *output = page->addGroup("Output");
    auto        *copyAction = new QAction("Copy to Clipboard", this);
    setThemedIcon(copyAction, Icon::Copy);
    copyAction->setToolTip("Copy the generated assembly to the clipboard");
    output->addSmall(copyAction);
    connect(copyAction, &QAction::triggered, compilerTab,
            [compilerTab] { compilerTab->onCopyToMainEditorClicked(); });
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
    // WP-02: the per-cycle trace of the current run, as JSON.
    QAction *exportTraceAction = new QAction("Export trace...", this);
    exportTraceAction->setObjectName("exportTraceAction");
    setActionHint(exportTraceAction, "Save the pipeline trace of this run as JSON");
    connect(exportTraceAction, &QAction::triggered, this,
            [this]()
            {
                const QString path = QFileDialog::getSaveFileName(
                    this, "Export trace", "trace.json", "JSON (*.json)");
                if (!path.isEmpty())
                    exportTrace(path);
            });
    fileMenu->addAction(exportTraceAction);
    // The reordering scheduler (by Nandan Nihal, CS25BTECH11014) behind its
    // measured safety gate. Also on the Editor page's Schedule group.
    m_suggestReorderAction = new QAction("Suggest reordering", this);
    m_suggestReorderAction->setObjectName("suggestReorderAction");
    setThemedIcon(m_suggestReorderAction, Icon::Reorder, largeIconSize);
    setActionHint(m_suggestReorderAction,
                  "Suggest a faster, equivalent order for this program's instructions");
    connect(m_suggestReorderAction, &QAction::triggered, this,
            &MainWindow::showReorderSuggestion);
    fileMenu->addAction(m_suggestReorderAction);
    fileMenu->addSeparator();
    fileMenu->addAction(exitAction);
    ////////////Setting Menu////////////////
    settingsMenu->addMenu(preferencesMenu);
    QActionGroup *themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    bool firstTheme = true;
    ThemeAppearance lastAppearance = ThemeAppearance::Dark;
    for (const ThemeData &theme : ThemeManager::getInstance().availableThemes())
    {
        // Dark themes, then light ones (availableThemes' order), split by a line.
        if (!firstTheme && theme.appearance != lastAppearance)
            preferencesMenu->addSeparator();
        firstTheme     = false;
        lastAppearance = theme.appearance;
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
    setThemedIcon(wireStayActiveAction, Icon::Wires);
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
    connect(aboutAction, &QAction::triggered, this,
            [this]()
            {
                AboutDialog dialog(this);
                dialog.exec();
            });

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
                        m_programName = QFileInfo(filename).fileName();
                        auto editorTab =
                            dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]);
                        if (editorTab)
                        {
                            // Not setRawText: opening a file also drops the
                            // breakpoints, which were line numbers in the
                            // file that is being replaced.
                            editorTab->loadFileContent(content);
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
    if (auto *cacheTab = dynamic_cast<CacheTab *>(m_tabs[TabIndex::CacheTabIndex]))
    {
        // The live cache view may take a full copy of the caches only when
        // nothing is changing them. A clock or state change emitted on the
        // GUI thread is a manual step, an undo/redo or a load: the GUI
        // thread did it, so the caches are idle. One emitted by the worker
        // is a run in progress, which the observers already follow access
        // by access, so it is ignored here (Direct, to see which thread).
        const auto resyncIfIdle = [cacheTab]
        {
            if (QThread::currentThread() == cacheTab->thread())
                cacheTab->resyncLiveView();
        };
        connect(m_processorManager, &ProcessorManager::processorClockedSignal, cacheTab,
                resyncIfIdle, Qt::DirectConnection);
        connect(m_processorManager, &ProcessorManager::processorStateChangedSignal, cacheTab,
                resyncIfIdle, Qt::DirectConnection);
        // Queued from the worker: delivered after it has left the run.
        connect(m_processorManager, &ProcessorManager::runFinishedSignal, cacheTab,
                &CacheTab::resyncLiveView, Qt::QueuedConnection);
        // A lab preset also sets the step delay, through the window's own
        // spin box so it shows what the processor uses.
        connect(cacheTab, &CacheTab::labPresetChosenSignal, this,
                [this] { m_stepDelaySpin->setValue(static_cast<int>(kLabPresetStepDelayMs)); });
        cacheTab->setLabRunInfoProvider(
            [this]
            {
                CacheTab::LabRunInfo info;
                info.programName = m_programName.toStdString();
                info.source      = m_loadedSource;
                if (info.source.empty())
                    if (auto *editorTab = dynamic_cast<EditorTab *>(m_tabs[TabIndex::EditorTabIndex]))
                        info.source = editorTab->getRawText();
                info.processor = processorDescription(m_processorManager->getProcessorType());
                info.cycles    = m_processorManager->getCycles();
                info.instructionsRetired = m_processorManager->getInstructionsRetired();
                // A run stopped early is reset, and a paused one has not
                // ended: only a program that ran to its end counts.
                info.completed = m_processorManager->programEnded();
                return info;
            });
    }
    m_tabs[TabIndex::CompilerTabIndex]  = new CompilerTab(this);
    m_tabs[TabIndex::ProfilerTabIndex]  = new ProfilerTab(this, m_processorManager->getProfiler());
    static_cast<ProfilerTab *>(m_tabs[TabIndex::ProfilerTabIndex])
        ->setProcessorManager(m_processorManager);
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
    if (m_timingPanel)
        m_timingPanel->clearSelection(); // a new trace: the old cell's sentence is void
    if (m_outputDock)
        m_outputDock->clearOutput(); // every Run, Debug Run and reload starts clean
    editor->setCanWrite(false);   // we disable writing in editor while vm is running
    if (m_saveAction)
        m_saveAction->setEnabled(false); // read-only editor: a disabled QAction also
                                         // swallows its Ctrl+S shortcut
    if (!m_processorManager->reset())
    {
        // The worker did not stop, so NOTHING was reset. Loading a program
        // onto a processor that is still running is the race this guards
        // against, so stop here instead.
        editor->setCanWrite(true);
        if (m_saveAction)
            m_saveAction->setEnabled(true);
        QMessageBox::critical(this, "Error",
                              "The processor did not stop. The program was not loaded.\n"
                              "Try again, or restart the application.");
        return false;
    }
    try
    {
        std::string rawText = editor->getRawText();
        m_loadedSource      = rawText;
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
        if (auto *cacheTab = dynamic_cast<CacheTab *>(m_tabs[TabIndex::CacheTabIndex]))
            cacheTab->resyncLiveView(); // the load reset the caches; no run yet
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
    // as soon s as run is clicked we disable the execution buttons
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
    if (m_timingPanel)
        m_timingPanel->clearSelection();
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
    // The scene belongs to the processor, so a new processor brings a new
    // scene that has never been told what is selected.
    if (CircuitScene *scene = m_processorManager->getCircuitScene())
        scene->setSelectedStages(m_processorManager->selectedStages());

    // Last, and from the manager rather than from vmType: changeProcessor
    // leaves m_currentProcessorType alone if the worker did not stop
    // (processor_manager.cpp:62-66), and the status bar must say what is
    // loaded, not what was asked for.
    updateProcessorStatusLabel();

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
    m_stepAwaitingInput = false;
    if (m_outputDock)
        m_outputDock->endInput(); // a Stop ends a read's wait; close its line

    // A program that ended keeps its final state (known_issues 142), and
    // Undo/Redo walk back and forth from it, however it was run.
    if (m_processorManager->programEnded())
    {
        setRunStatus("Program finished");
        m_undoAction->setEnabled(true);
        m_redoAction->setEnabled(true);
    }
    else
    {
        setRunStatus(m_userStopped ? "Stopped" : "Ready");
    }
    m_userStopped = false;
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
    // action in the window - ribbon, menus and submenus alike - with no
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
    // Best effort on the way out. If it times out there is nothing left
    // to do but warn - StopAndReset has already declined to clear state
    // under a live worker, which is the safe half.
    if (!m_processorManager->stop())
        qWarning("Atlas: processor did not stop during shutdown");
    if(m_processorThread->isRunning())
    {
        m_processorThread->quit();
        m_processorThread->wait();
    }
    // delete ui;
}

bool MainWindow::exportTrace(const QString &path)
{
    QString error;
    if (m_processorManager->exportTraceJson(path, &error))
        return true;
    QMessageBox::warning(this, "Export trace", "Could not write the trace:\n" + error);
    return false;
}
} // namespace Atlas
