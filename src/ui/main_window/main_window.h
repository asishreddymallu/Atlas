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
