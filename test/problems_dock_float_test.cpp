/**
 * @file problems_dock_float_test.cpp
 * @brief final/polish: the Problems dock floats - float button, resizable,
 *        and its floating state and geometry remembered between runs - and
 *        Ctrl+Shift+M and the ribbon button still show and hide it.
 *        Mirrors TimingOverlay.HeightIsRememberedBetweenRuns.
 */
#include "ui/main_window/main_window.h"
#include "ui/problems_panel/problems_dock.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QSettings>

#include <gtest/gtest.h>

using namespace Atlas;

namespace
{
constexpr const char *kFloatingKey = "problemsDock/floating";
constexpr const char *kGeometryKey = "problemsDock/geometry";

void settle()
{
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
}

void clearSettings()
{
    QSettings s("Atlas", "Atlas");
    s.remove(kFloatingKey);
    s.remove(kGeometryKey);
}

QAction *problemsToggle(MainWindow &w)
{
    for (QAction *a : w.findChildren<QAction *>())
        if (a->text() == QStringLiteral("Problems") && a->shortcut() == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M))
            return a;
    return nullptr;
}
} // namespace

TEST(ProblemsDockFloat, ItHasTheFloatButtonAndFloatsResizably)
{
    clearSettings();
    MainWindow window;
    window.show();
    auto *dock = window.findChild<ProblemsDock *>();
    ASSERT_NE(dock, nullptr);
    EXPECT_TRUE(dock->features().testFlag(QDockWidget::DockWidgetFloatable));
    EXPECT_TRUE(dock->features().testFlag(QDockWidget::DockWidgetMovable));
    EXPECT_TRUE(dock->features().testFlag(QDockWidget::DockWidgetClosable));
    dock->show();
    settle();
    auto *floatButton = dock->findChild<QAbstractButton *>(QStringLiteral("qt_dockwidget_floatbutton"));
    ASSERT_NE(floatButton, nullptr);
    EXPECT_TRUE(floatButton->isVisible());

    floatButton->click();
    settle();
    EXPECT_TRUE(dock->isFloating());
    dock->resize(520, 300);
    settle();
    EXPECT_EQ(dock->size(), QSize(520, 300));
    EXPECT_EQ(dock->minimumSize(), QSize(ProblemsDock::kFloatingMinWidth, ProblemsDock::kFloatingMinHeight));
    dock->setFloating(false);
    settle();
    EXPECT_FALSE(dock->isFloating());
    clearSettings();
}

TEST(ProblemsDockFloat, FloatingStateAndGeometryAreRememberedBetweenRuns)
{
    clearSettings();
    QRect left;
    {
        MainWindow window;
        window.show();
        auto *dock = window.findChild<ProblemsDock *>();
        dock->show();
        dock->setFloating(true);
        settle();
        dock->setGeometry(140, 120, 560, 260);
        settle();
        left = dock->geometry();
        QSettings s("Atlas", "Atlas");
        EXPECT_TRUE(s.value(kFloatingKey).toBool());
        EXPECT_EQ(s.value(kGeometryKey).toRect(), left);
    }
    {
        MainWindow window;
        window.show();
        auto *dock = window.findChild<ProblemsDock *>();
        EXPECT_FALSE(dock->isVisible()) << "still hidden until asked for";
        QAction *toggle = problemsToggle(window);
        ASSERT_NE(toggle, nullptr);
        toggle->trigger(); // what Ctrl+Shift+M and the ribbon button do
        settle();
        EXPECT_TRUE(dock->isVisible());
        EXPECT_TRUE(dock->isFloating());
        EXPECT_EQ(dock->geometry(), left);
        // Docked again, the next run starts docked.
        dock->setFloating(false);
        settle();
        EXPECT_FALSE(QSettings("Atlas", "Atlas").value(kFloatingKey).toBool());
    }
    {
        MainWindow window;
        window.show();
        auto *dock = window.findChild<ProblemsDock *>();
        problemsToggle(window)->trigger();
        settle();
        EXPECT_FALSE(dock->isFloating());
    }
    clearSettings();
}

TEST(ProblemsDockFloat, CtrlShiftMStillShowsAndHidesIt)
{
    clearSettings();
    MainWindow window;
    window.show();
    auto    *dock   = window.findChild<ProblemsDock *>();
    QAction *toggle = problemsToggle(window);
    ASSERT_NE(toggle, nullptr);
    EXPECT_EQ(toggle->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M));
    toggle->trigger();
    settle();
    EXPECT_TRUE(dock->isVisible());
    toggle->trigger();
    settle();
    EXPECT_FALSE(dock->isVisible());
}
