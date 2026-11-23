/**
 * @file cache_window_harness.h
 * @brief The Cache tab test harness: the real window, RVSS, the window's own
 *        Run, and the Statistics fields read back.
 *
 * The same harness as the CacheWindow in cache_tab_gui_test.cpp (which keeps
 * it in an anonymous namespace there, and is left untouched), so the new
 * cache-lab tests measure exactly as the existing Cache tab tests do.
 */
#ifndef ATLAS_TEST_CACHE_WINDOW_HARNESS_H
#define ATLAS_TEST_CACHE_WINDOW_HARNESS_H

#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "ui/cache_tab/cache_config_widget.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QTimer>

#include <functional>

namespace Atlas::test_support
{
inline bool waitUntilWindow(const std::function<bool()> &done, int ms = 120000)
{
    QElapsedTimer clock;
    clock.start();
    while (!done())
    {
        if (clock.elapsed() > ms)
            return false;
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

inline void spinWindow(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

inline QString readCacheLabExample(const QString &name)
{
    QFile f(QStringLiteral("../examples/cache_lab/%1.s").arg(name));
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

struct CacheWindow
{
    QTimer            modalTimer;
    QStringList       modals;
    MainWindow        window;
    Editor           *editor{nullptr};
    QAction          *run{nullptr}, *debug{nullptr};
    ProcessorManager *manager{nullptr};
    CacheTab         *cacheTab{nullptr};

    CacheWindow()
    {
        QObject::connect(&modalTimer, &QTimer::timeout, [this] {
            if (QWidget *modal = QApplication::activeModalWidget())
            {
                auto *box = qobject_cast<QMessageBox *>(modal);
                modals << (box ? box->text() : modal->windowTitle());
                modal->close();
            }
        });
        modalTimer.start(50);
        window.resize(1200, 800);
        window.show();
        QApplication::processEvents();
        for (QPlainTextEdit *pane : window.findChild<EditorTab *>()->findChildren<QPlainTextEdit *>())
            if (!pane->isReadOnly())
                editor = static_cast<Editor *>(pane);
        for (QAction *a : window.findChildren<QAction *>(Qt::FindDirectChildrenOnly))
        {
            if (a->text() == "Run") run = a;
            if (a->text() == "Debug Run") debug = a;
        }
        if (auto *panel = window.findChild<TimingGridPanel *>("timingGridPanel"))
            manager = panel->manager();
        cacheTab = window.findChild<CacheTab *>();
    }
    bool ready() const { return editor && run && debug && manager && cacheTab; }

    CacheConfigWidget *level(const char *name) { return window.findChild<CacheConfigWidget *>(name); }

    void use(ProcessorType type)
    {
        window.processorChanged(type);
        manager->setStepDelay(0); // the spin box stops at 1 ms; these runs are long
        QApplication::processEvents();
    }

    bool runSource(const QString &source)
    {
        manager->setStepDelay(0);
        editor->setPlainText(source);
        QApplication::processEvents();
        run->trigger();
        const bool done = waitUntilWindow([this] { return debug->isEnabled() && run->text() == "Run"; });
        spinWindow(50); // the queued stats update
        return done;
    }

    QString field(const char *levelName, const char *edit)
    {
        CacheConfigWidget *w = level(levelName);
        QLineEdit         *e = w ? w->findChild<QLineEdit *>(edit) : nullptr;
        return e ? e->text() : QStringLiteral("<no field %1>").arg(edit);
    }
};
} // namespace Atlas::test_support

#endif // ATLAS_TEST_CACHE_WINDOW_HARNESS_H
