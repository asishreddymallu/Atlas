#pragma once
#include "processor/processor_manager.h"
#include "ui/common/kites_tab.h"
#include "ui/problems_panel/problems_dock.h"
#include "ui/register_table/register_container.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/theme/theme_manager.h"
#include "utils/to_index.h"
#include <QAction>
#include <QMainWindow>
#include <QStackedWidget>
#include <QThread>
#include <QWidget>

class QSpinBox;

namespace Kites
{
QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    //not using enum class here because we want to use the enum values as array indices
    // and if we using enum class we will have to static cast them to size_t/int every time
    enum TabIndex
    {
        EditorTabIndex = 0,
        MemoryTabIndex,
        ProcessorTabIndex,
        CacheTabIndex,
        CompilerTabIndex,
        ProfilerTabIndex,
        TabCount
    };

    MainWindow(QWidget *parent = nullptr);
    void setUpUI();
    ~MainWindow();

    /// The single writer for the visible tab. Moves the ribbon strip, the
    /// ribbon page and the content stack together, so a programmatic switch
    /// cannot leave the strip pointing at a tab that is not on screen.
    /// Out-of-range indices are ignored.
    void setCurrentTab(TabIndex index);

    /// The visible tab. The content stack holds this; nothing caches it.
    TabIndex currentTab() const;

private:
    enum class ExecutionState
    {
        Running,
        Stopped,
        Paused
    };

    void setUpStatusBar();
    void setUpToolBar();
    void setUpRibbon();
    void setUpPersistentGroups();
    void setUpEditorPage();
    void setUpMemoryPage();
    void setUpProcessorPage();
    void setUpCachePage();
    void setUpCompilerPage();
    void setUpMenubar();
    void setUpProblemsDock();
    void setUpTabs();
    void toggleTheme(const QString &themeId);
    bool tryParseAndLoadProgram();
    void run(bool startPaused = false);
    void processorChangeDialog();
    Ui::MainWindow *ui;

    // QToolBar *m_mainToolbar = nullptr;
    // held so Save can be disabled while the editor is read-only (i.e. while a
    // program is running); a disabled QAction also swallows its shortcut
    QAction           *m_saveAction        {nullptr};
    QAction           *m_openAction        {nullptr};
    // the Problems dock's own toggleViewAction, held so the ribbon can
    // show the same action the View menu does
    QAction           *m_problemsToggleAction {nullptr};
    // The ribbon's View group. These forward to EditorTab's existing slots
    // and mirror its in-tab buttons; the buttons stay the state.
    QAction           *m_editorViewAction   {nullptr};
    QAction           *m_expandedViewAction {nullptr};
    // Settings > Processor > Wires Stay Active, held so the Processor page
    // can show the menu's own action rather than a second checkbox for the
    // same setting. Created where it always was, in setUpMenubar().
    QAction           *m_wiresStayActiveAction {nullptr};
    // The execution controls, held rather than left local to setUpToolBar().
    // runFinishedSlot() used to recover them by walking
    // findChildren<QToolBar*>()[0] and comparing action->text() - text these
    // same handlers rewrite at runtime (Run<->Stop, Pause<->Resume). That is
    // only safe while exactly one toolbar exists and no action is shown
    // twice, and the ribbon makes both assumptions false.
    QAction           *m_processorAction   {nullptr};
    QAction           *m_runAction         {nullptr};
    QAction           *m_pauseAction       {nullptr};
    QAction           *m_debugAction       {nullptr};
    QAction           *m_stepAction        {nullptr};
    QAction           *m_undoAction        {nullptr};
    QAction           *m_redoAction        {nullptr};
    QSpinBox          *m_stepDelaySpin     {nullptr};
    ProblemsDock      *m_problemsDock     {nullptr};
    RibbonBar         *m_ribbon            {nullptr};
    QStackedWidget    *m_stackedTabs       {nullptr};
    RegisterContainer *m_registerContainer {nullptr};
    ProcessorManager  *m_processorManager  {nullptr};
    QThread           *m_processorThread   {nullptr};
    ExecutionState     m_executionState    {ExecutionState::Stopped};

    std::array<KitesTab*, toIndex(TabIndex::TabCount)> m_tabs;
public slots:
    void processorChanged(const ProcessorType &vmType); // this will catch the signal from processor dialog
    void runFinishedSlot();
    void themeChangedSlot(const QString &themeId);
    // void runErrorSlot();
signals:
    void processorChangedSignal();
    void runProcessorSignal();
    void debugRunProcessorSignal();
};
} // namespace Kites
