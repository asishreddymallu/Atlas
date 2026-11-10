#include "mainwindow.h"
#include "assembler/assembler.h"
#include "common/assembled_program.h"
#include "common/globals.h"
#include "custom_pseudo_manager/custom_pseudo_manager.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/compiler_tab/compiler_tab.h"
#include "ui/dialogs/processor_dialog.h"
#include "ui/dialogs/settings_dialog.h"
#include "ui/editor_tab/editortab.h"
#include "ui/memory_tab/memory_tab.h"
#include "ui/processor_tab/processortab.h"
#include "ui/profiler_tab/profilertab.h"
#include "ui/theme/icon_manager.h"
#include "ui_mainwindow.h"
#include "utils/utils.h"
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QWidgetAction>
#include <QSplitter>
#include <QMessageBox>

namespace Kites
{
namespace
{
// Chrome icon size in logical pixels, shared by the toolbar and the sidebar so
// the two never disagree. IconManager's default matches it.
constexpr int toolbarIconSize = 20;

// Keeps the shortcut hint and the tooltip in one place, so a tooltip reworded
// at runtime (Run<->Stop, Pause<->Resume) cannot lose its key hint.
void setActionHint(QAction *action, const QString &description)
{
    const QString keys = action->shortcut().toString(QKeySequence::NativeText);
    action->setToolTip(keys.isEmpty() ? description
                                      : QStringLiteral("%1  (%2)").arg(description, keys));
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
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    m_stackedTabs = new QStackedWidget(this);
    m_sidebar = new QListWidget(this);
    setUpStatusBar();
    setUpSidebar();
    setUpToolBar();
    setUpMenubar();
    setUpTabs();
    setUpProblemsDock();

    // m_registerContainer = new RegisterContainer(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(m_stackedTabs);
    splitter->addWidget(m_registerContainer);
    splitter->widget(1)->setMaximumWidth(350);

    // outer splitter makes the sidebar width user-adjustable (Zed-style)
    QSplitter *outerSplitter = new QSplitter(Qt::Horizontal, this);
    outerSplitter->addWidget(m_sidebar);
    outerSplitter->addWidget(splitter);
    outerSplitter->setCollapsible(1, false);
    outerSplitter->setStretchFactor(0, 0);
    outerSplitter->setStretchFactor(1, 1);
    outerSplitter->setSizes({m_sidebar->sizeHintForColumn(0) + 24, 1000});

    mainLayout->addWidget(outerSplitter);

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
    QAction  *processorAction = new QAction("Processor", this);
    QAction  *runAction       = new QAction("Run", this);
    QAction  *pauseAction     = new QAction("Pause", this);
    QAction  *debugAction     = new QAction("Debug Run", this);
    QAction  *stepAction      = new QAction("Step", this);
    QAction  *undoAction      = new QAction("Undo", this);
    QAction  *redoAction      = new QAction("Redo", this);

    stepAction->setDisabled(true);
    pauseAction->setDisabled(true);
    undoAction->setDisabled(true);
    redoAction->setDisabled(true);

    // set toolbar icons (resources in :/icons)
    setThemedIcon(processorAction, Icon::Processor);
    setThemedIcon(runAction, Icon::Play);
    setThemedIcon(pauseAction, Icon::Pause);
    setThemedIcon(undoAction, Icon::Undo);
    setThemedIcon(redoAction, Icon::Redo);
    setThemedIcon(stepAction, Icon::Step);
    setThemedIcon(debugAction, Icon::Debug);
    // Keyboard shortcuts. None of these collide with QPlainTextEdit's built-in
    // bindings (Ctrl+C/X/V/Z/Y/A and friends) or with the keys Editor
    // intercepts for the completer popup (Enter/Return/Escape/Tab/Backtab).
    // Toolbar Undo/Redo are deliberately left unbound: they undo a VM step,
    // not a text edit, and Ctrl+Z in the editor must stay text undo.
    runAction->setShortcut(QKeySequence(Qt::Key_F5));
    debugAction->setShortcut(QKeySequence(Qt::Key_F9));
    pauseAction->setShortcut(QKeySequence(Qt::Key_F6));
    stepAction->setShortcut(QKeySequence(Qt::Key_F10));

    // tooltips for accessibility
    setActionHint(processorAction, "Processor settings");
    setActionHint(runAction, "Run program");
    setActionHint(pauseAction, "Pause execution");
    setActionHint(debugAction, "Start debug run");
    setActionHint(stepAction, "Execute single step");
    setActionHint(undoAction, "Undo last VM step");
    setActionHint(redoAction, "Redo last VM step");
    // one icon size for the whole chrome; the sidebar uses the same value
    toolbar->setIconSize(QSize(toolbarIconSize, toolbarIconSize));
    // single fixed toolbar: the drag handle is just visual noise
    toolbar->setMovable(false);

    //------------Spinbox for setting execution speed----------------
    QSpinBox *spinbox = new QSpinBox(this);
    spinbox->setRange(1, 10000);
    spinbox->setValue(1000);
    spinbox->setSuffix(" ms");
    spinbox->setToolTip("Set Execution Speed (in milliseconds)");
    //---------------------------------------------------------------

    QWidgetAction *spinboxAction = new QWidgetAction(this);
    spinboxAction->setDefaultWidget(spinbox);
    // identical action order to before; the separators only group them visually
    toolbar->addAction(processorAction);   // processor settings
    toolbar->addSeparator();
    toolbar->addAction(runAction);         // start / stop
    toolbar->addAction(debugAction);
    toolbar->addSeparator();
    toolbar->addAction(spinboxAction);     // step interval
    toolbar->addSeparator();
    toolbar->addAction(pauseAction);       // transport
    toolbar->addAction(stepAction);
    toolbar->addSeparator();
    toolbar->addAction(undoAction);        // history
    toolbar->addAction(redoAction);

    connect(runAction, &QAction::triggered, this,
            [this, processorAction, runAction, pauseAction, debugAction]()
            {
                if (runAction->text() == "Run")
                {
                    debugAction->setDisabled(true);
                    processorAction->setDisabled(true);
                    pauseAction->setEnabled(true);
                    runAction->setText("Stop");
                    setThemedIcon(runAction, Icon::Stop);
                    setActionHint(runAction, "Stop execution");
                    this->run();
                }
                else if (runAction->text() == "Stop")
                {
                    // emit stopSignal();
                    runAction->setText("Run");
                    setThemedIcon(runAction, Icon::Play);
                    setActionHint(runAction, "Run program");
                    m_processorManager->stop();
                }
            });

    connect(debugAction, &QAction::triggered, this,
            [this, processorAction, runAction, pauseAction, stepAction, undoAction, redoAction,
             debugAction]()
            {
                debugAction->setDisabled(true);
                processorAction->setDisabled(true);
                // debug run starts paused, so the toolbar starts in the paused state
                runAction->setText("Stop");
                setThemedIcon(runAction, Icon::Stop);
                setActionHint(runAction, "Stop execution");
                pauseAction->setEnabled(true);
                pauseAction->setText("Resume");
                setThemedIcon(pauseAction, Icon::Resume);
                setActionHint(pauseAction, "Resume execution");
                stepAction->setEnabled(true);
                undoAction->setEnabled(true);
                redoAction->setEnabled(true);
                this->run(true);
            });

    connect(processorAction, &QAction::triggered, this, &MainWindow::processorChangeDialog);

    connect(spinbox, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int value) { m_processorManager->setStepDelay(value); });

    connect(pauseAction, &QAction::triggered, this,
            [this, pauseAction, stepAction, undoAction, redoAction]()
            {
                if (pauseAction->text() == "Pause")
                {
                    pauseAction->setText("Resume");
                    setThemedIcon(pauseAction, Icon::Resume);
                    setActionHint(pauseAction, "Resume execution");
                    stepAction->setEnabled(true);
                    undoAction->setEnabled(true);
                    redoAction->setEnabled(true);
                    m_processorManager->pause();
                }
                else
                {
                    pauseAction->setText("Pause");
                    setThemedIcon(pauseAction, Icon::Pause);
                    setActionHint(pauseAction, "Pause execution");
                    stepAction->setDisabled(true);
                    undoAction->setDisabled(true);
                    redoAction->setDisabled(true);
                    m_processorManager->resume();
                }
            });

    // we also connect the vm paused at breakpoint signal to change the pause button text
    connect(m_processorManager, &ProcessorManager::processorPausedAtBreakpointSignal, this,
            [this, pauseAction, stepAction, undoAction, redoAction]()
            {
                pauseAction->setText("Resume");
                setThemedIcon(pauseAction, Icon::Resume);
                setActionHint(pauseAction, "Resume execution");
                stepAction->setEnabled(true);
                undoAction->setEnabled(true);
                redoAction->setEnabled(true);
            });

    connect(stepAction, &QAction::triggered, this, [this]() { m_processorManager->step(); });
    connect(undoAction, &QAction::triggered, this, [this]() { m_processorManager->undo(); });
    connect(redoAction, &QAction::triggered, this, [this]() { m_processorManager->redo(); });
}

void MainWindow::setUpSidebar()
{
    auto &iconManager = IconManager::getInstance();
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Editor), "Editor"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Memory), "Memory"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Processor), "Processor"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Cache), "Cache"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Compiler), "Compiler"));
    m_sidebar->addItem(new QListWidgetItem(iconManager.getIcon(Icon::Profiler), "Profiler"));
    m_sidebar->setIconSize(QSize(toolbarIconSize, toolbarIconSize));
    m_sidebar->setMinimumWidth(48);
    m_sidebar->setCurrentRow(0);
    // m_sidebar->setFocusPolicy(Qt::ClickFocus);

    connect(m_sidebar, &QListWidget::currentRowChanged, m_stackedTabs,
            &QStackedWidget::setCurrentIndex);
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
    setThemedIcon(openAction, Icon::Open);
    setThemedIcon(saveAction, Icon::Save);
    openAction->setShortcut(QKeySequence::Open);  // Ctrl+O
    saveAction->setShortcut(QKeySequence::Save);  // Ctrl+S
    setActionHint(openAction, "Open an assembly file");
    setActionHint(saveAction, "Save the current program");
    m_saveAction = saveAction;
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
    QList<QToolBar *> toolbars = this->findChildren<QToolBar *>();
    // since we only have one toolbar we can directly access it
    for (QAction *action : toolbars[0]->actions())
    {
        if (action->text() == "Run" || action->text() == "Processor" ||
            action->text() == "Debug Run")
        {
            action->setEnabled(true);
        }
        if (action->text() == "Stop")
        {
            action->setText("Run");
            setThemedIcon(action, Icon::Play);
            action->setToolTip("Run program");
            // when the program finishes we set the run button text back to run
        }
        if (action->text() == "Resume")
        {
            action->setText("Pause");
            setThemedIcon(action, Icon::Pause);
            action->setToolTip("Pause execution");
            // in case we stop while paused
        }

        if (action->text() == "Pause" || action->text() == "Stop" || action->text() == "Step")
        {
            action->setDisabled(true);
        }
    }

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
            setThemedIcon(action, icon);
    }

    // sidebar entries are QListWidgetItems, not actions; they are already
    // keyed by row rather than by label
    static const std::array<Icon, toIndex(TabIndex::TabCount)> sidebarIcons = {
        Icon::Editor, Icon::Memory, Icon::Processor, Icon::Cache, Icon::Compiler, Icon::Profiler};
    for (int row = 0; row < m_sidebar->count(); ++row)
    {
        if (row < static_cast<int>(sidebarIcons.size()))
            m_sidebar->item(row)->setIcon(IconManager::getInstance().getIcon(sidebarIcons[row]));
    }
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
