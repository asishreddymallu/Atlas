/**
 * @file cache_tab_gui_test.cpp
 * @brief T5: the Cache tab in the real window - the Statistics box and the
 *        configuration it shows and sends to the engine.
 *
 * The three sub-tabs (Instruction Cache, L1 Cache, L2 Cache) are three
 * CacheConfigWidgets, "InstructionConfig", "L1Config" and "L2Config". The
 * programs are the E8 cache-lab benchmarks (examples/cache_lab), run in the
 * window with the window's own Run action. Expected numbers are the E8
 * Python oracle's (~/atlas-verify/e8, also in test/cache_lab_cases.inc):
 * dm256 is 2^n Lines 4, 2^n Ways 0, 2^n words/line 2; 2w256 is 3, 1, 2;
 * both Write Through + Write No Allocate. The data-cache counts are the
 * same on all five models (E8), so RVSS is used: fewest cycles.
 *
 * Known issue 83 stands: Undo does not restore the cache counters, so
 * nothing here checks the Statistics box after an Undo.
 */
#include "processor/cache/cache.h"
#include "processor/memory_controller.h"
#include "processor/processor_manager.h"
#include "processor/processor_types.h"
#include "ui/cache_tab/cache_config_widget.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QTimer>

#include <cmath>
#include <functional>
#include <string>

using namespace Kites;

// QString compared as std::string, so a failure prints the text.
#define EXPECT_EQ_Q(a, b) EXPECT_EQ((a).toStdString(), std::string(b))

namespace
{
bool waitUntil(const std::function<bool()> &done, int ms = 50000)
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

void spin(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
        QApplication::processEvents(QEventLoop::AllEvents, 10);
}

QString readExample(const char *name)
{
    QFile f(QString("../examples/cache_lab/%1.s").arg(name));
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
    }
    bool ready() const { return editor && run && debug && manager; }

    CacheConfigWidget *level(const char *name) { return window.findChild<CacheConfigWidget *>(name); }

    /// Sets a level's fields through its own widgets, as a user would.
    void configure(const char *name, int lines, int ways, int words, const char *writeHit,
                   const char *writeMiss, const char *replacement)
    {
        CacheConfigWidget *w = level(name);
        w->findChild<QSpinBox *>("linesSpinBox")->setValue(lines);
        w->findChild<QSpinBox *>("waysSpinBox")->setValue(ways);
        w->findChild<QSpinBox *>("wordsSpinBox")->setValue(words);
        auto pick = [w](const char *combo, const char *text) {
            QComboBox *c = w->findChild<QComboBox *>(combo);
            c->setCurrentIndex(c->findText(text));
        };
        pick("writeHitComboBox", writeHit);
        pick("writeMissComboBox", writeMiss);
        pick("repPolComboBox", replacement);
        QApplication::processEvents();
    }

    /// Switch the model first: the configuration is set on the caches the
    /// window has after the switch.
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
        const bool done = waitUntil([this] { return debug->isEnabled() && run->text() == "Run"; });
        spin(50); // the queued stats update
        return done;
    }

    QString field(const char *levelName, const char *edit)
    {
        CacheConfigWidget *w = level(levelName);
        QLineEdit         *e = w ? w->findChild<QLineEdit *>(edit) : nullptr;
        return e ? e->text() : QStringLiteral("<no field %1>").arg(edit);
    }
};

// One hit in three: a miss fills the 16-byte line at 0x10000, the next word
// hits it, the third load misses on the next line.
const char *kOneInThree = ".text\n"
                          "    lui x5, 0x10\n"
                          "    addi x0, x0, 0\n"
                          "    addi x0, x0, 0\n"
                          "    lw x6, 0(x5)\n"
                          "    lw x7, 4(x5)\n"
                          "    lw x8, 16(x5)\n";

const char *kNoMemory = ".text\n    addi x5, x0, 1\n";

void dm256(CacheWindow &w)
{
    w.use(ProcessorType::RVSS);
    w.configure("L1Config", 4, 0, 2, "Write Through", "Write No Allocate", "LRU");
}
} // namespace

// ---- Item 1: the hit rate is shown as a percentage --------------------------

TEST(CacheTabHitRate, Stride1ReadsSeventyFivePercent)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    dm256(w);
    ASSERT_TRUE(w.runSource(readExample("stride_1")));
    EXPECT_EQ_Q(w.field("L1Config", "sizeLineEdit"), "256 bytes");
    EXPECT_EQ_Q(w.field("L1Config", "hitsLineEdit"), "768");
    EXPECT_EQ_Q(w.field("L1Config", "missesLineEdit"), "256");
    EXPECT_EQ_Q(w.field("L1Config", "hitrateLineEdit"), "75.00 %");
}

TEST(CacheTabHitRate, Stride16ReadsZeroPercent)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    dm256(w);
    ASSERT_TRUE(w.runSource(readExample("stride_16")));
    EXPECT_EQ_Q(w.field("L1Config", "hitsLineEdit"), "0");
    EXPECT_EQ_Q(w.field("L1Config", "missesLineEdit"), "64");
    EXPECT_EQ_Q(w.field("L1Config", "hitrateLineEdit"), "0.00 %");
}

TEST(CacheTabHitRate, OneHitInThreeReadsAPercentageWithTwoDecimals)
{
    CacheWindow w;
    ASSERT_TRUE(w.ready());
    dm256(w);
    ASSERT_TRUE(w.runSource(kOneInThree));
    EXPECT_EQ_Q(w.field("L1Config", "hitsLineEdit"), "1");
    EXPECT_EQ_Q(w.field("L1Config", "missesLineEdit"), "2");
    EXPECT_EQ_Q(w.field("L1Config", "hitrateLineEdit"), "33.33 %");
}
