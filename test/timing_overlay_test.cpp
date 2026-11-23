/**
 * @file timing_overlay_test.cpp
 * @brief T6: View > Pipeline timing is a panel laid over the central area.
 *
 * Opening or closing it must not move or resize any other widget; it is
 * anchored to the bottom of the central area at full width, resized from its
 * top edge between 150 px and the space under the ribbon, and its height is
 * remembered through QSettings (the test sandbox's, see test_main.cpp).
 * Everything is found the way a user would reach it: the View menu's action,
 * the close button, the drag strip.
 */
#include "ui/editor_tab/editor_tab.h"
#include "ui/main_window/main_window.h"
#include "ui/output_panel/output_dock.h"
#include "ui/problems_panel/problems_dock.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/register_table/register_container.h"
#include "ui/ribbon/ribbon_bar.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QGraphicsView>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QSettings>

#include <string>

using namespace Atlas;

namespace
{
constexpr const char *kHeightKey = "timingOverlay/height";

void settle()
{
    for (int i = 0; i < 5; ++i)
        QApplication::processEvents();
}

struct Geometry
{
    QRect rect;
    bool  visible = false;
    bool  operator==(const Geometry &o) const { return rect == o.rect && visible == o.visible; }
};

struct OverlayWindow
{
    MainWindow window;
    QWidget   *overlay{nullptr};
    QAction   *toggle{nullptr};

    explicit OverlayWindow(QSize size = {1200, 800})
    {
        window.resize(size);
        window.show();
        settle();
        overlay = window.findChild<QWidget *>("timingDock");
        // The View menu's item, by its text, as a user finds it.
        for (QAction *menu : window.menuBar()->actions())
            if (menu->menu() && menu->text() == "&View")
                for (QAction *a : menu->menu()->actions())
                    if (a->text() == "Pipeline timing")
                        toggle = a;
    }
    bool ready() const { return overlay && toggle; }

    QAbstractButton *closeButton() const
    {
        return overlay ? overlay->findChild<QAbstractButton *>("timingOverlayClose") : nullptr;
    }
    QWidget *grip() const { return overlay ? overlay->findChild<QWidget *>("timingOverlayGrip") : nullptr; }
    QWidget *central() const { return window.centralWidget(); }
    RibbonBar *ribbon() const { return window.findChild<RibbonBar *>(); }

    /// Space under the ribbon in the central area: the largest height allowed.
    int maxHeight() const { return central()->height() - ribbon()->height(); }
    /// The overlay's rectangle in the central widget's coordinates. (It is a
    /// child of the window laid over the central widget, not inside it.)
    QRect inCentral() const
    {
        return {central()->mapFromGlobal(overlay->mapToGlobal(QPoint(0, 0))), overlay->size()};
    }

    /// Every widget of the window except the overlay and what it holds.
    QMap<QString, Geometry> snapshot() const
    {
        QMap<QString, Geometry> out;
        int                     n = 0;
        for (QWidget *w : window.findChildren<QWidget *>())
        {
            if (w->isWindow() || w == overlay || (overlay && overlay->isAncestorOf(w)))
                continue;
            const QString key = QString("%1#%2:%3")
                                    .arg(n++)
                                    .arg(QString::fromLatin1(w->metaObject()->className()))
                                    .arg(w->objectName());
            out[key] = {QRect(w->mapTo(&window, QPoint(0, 0)), w->size()), w->isVisible()};
        }
        return out;
    }

    /// Presses on the drag strip, moves it dy pixels and releases.
    void drag(int dy)
    {
        QWidget     *g = grip();
        const QPointF local(g->width() / 2.0, g->height() / 2.0);
        const QPointF global = g->mapToGlobal(local);
        QMouseEvent   press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton,
                            Qt::NoModifier);
        QApplication::sendEvent(g, &press);
        QMouseEvent move(QEvent::MouseMove, local + QPointF(0, dy), global + QPointF(0, dy), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(g, &move);
        QMouseEvent release(QEvent::MouseButtonRelease, local + QPointF(0, dy), global + QPointF(0, dy),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(g, &release);
        settle();
    }
};

/// The named widgets the stage asks about, so a missing one fails loudly
/// rather than silently leaving the all-widget comparison with less to see.
void expectKeyWidgets(const MainWindow &window)
{
    EXPECT_NE(window.findChild<EditorTab *>(), nullptr);
    EXPECT_NE(window.findChild<ProcessorTab *>(), nullptr);
    EXPECT_NE(window.findChild<RegisterContainer *>(), nullptr);
    EXPECT_NE(window.findChild<ProblemsDock *>(), nullptr);
    EXPECT_NE(window.findChild<OutputDock *>(), nullptr);
}

/// The widgets whose geometry or visibility differs, one per line.
std::string changed(const QMap<QString, Geometry> &before, const QMap<QString, Geometry> &after)
{
    std::string out;
    auto        rect = [](const QRect &r) {
        return QString("%1,%2 %3x%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    };
    for (auto it = before.cbegin(); it != before.cend(); ++it)
    {
        const Geometry now = after.value(it.key());
        if (!after.contains(it.key()) || !(now == it.value()))
            out += QString("  %1: %2%3 -> %4%5\n")
                       .arg(it.key(), rect(it.value().rect), it.value().visible ? "" : " (hidden)",
                            rect(now.rect), now.visible ? "" : " (hidden)")
                       .toStdString();
    }
    if (after.size() != before.size())
        out += "  widget count " + std::to_string(before.size()) + " -> " + std::to_string(after.size()) + "\n";
    return out;
}

void expectOpenAndCloseMoveNothing(OverlayWindow &w, const char *where)
{
    const auto before = w.snapshot();
    ASSERT_GT(before.size(), 50) << where; // a real window, not an empty list
    w.toggle->trigger();
    settle();
    ASSERT_TRUE(w.overlay->isVisible()) << where;
    EXPECT_EQ(changed(before, w.snapshot()), "") << where << ": opening moved or resized these";
    w.toggle->trigger();
    settle();
    ASSERT_FALSE(w.overlay->isVisible()) << where;
    EXPECT_EQ(changed(before, w.snapshot()), "") << where << ": closing moved or resized these";
}
} // namespace

TEST(TimingOverlay, OpeningAndClosingMovesNoOtherWidget)
{
    OverlayWindow w;
    ASSERT_TRUE(w.ready());
    expectKeyWidgets(w.window);

    // On the Editor tab, with no bottom panel open.
    w.ribbon()->setCurrentIndex(MainWindow::EditorTabIndex);
    settle();
    expectOpenAndCloseMoveNothing(w, "editor, no panels");

    // On the Processor tab, where the circuit is.
    w.ribbon()->setCurrentIndex(MainWindow::ProcessorTabIndex);
    settle();
    ASSERT_NE(w.window.findChild<ProcessorTab *>()->findChild<QGraphicsView *>(), nullptr);
    expectOpenAndCloseMoveNothing(w, "processor, no panels");

    // With the bottom panels (Problems and Output) open.
    w.window.findChild<ProblemsDock *>()->show();
    w.window.findChild<OutputDock *>()->show();
    settle();
    expectOpenAndCloseMoveNothing(w, "processor, bottom panels open");
    w.ribbon()->setCurrentIndex(MainWindow::EditorTabIndex);
    settle();
    expectOpenAndCloseMoveNothing(w, "editor, bottom panels open");
}

TEST(TimingOverlay, IsAnchoredToTheBottomAtFullWidthAndFollowsResizes)
{
    QSettings("Atlas", "Atlas").remove(kHeightKey);
    OverlayWindow w;
    ASSERT_TRUE(w.ready());
    w.toggle->trigger();
    settle();
    // Default: about 40% of the window.
    EXPECT_NEAR(w.overlay->height(), 0.4 * w.window.height(), 2);

    auto expectAnchored = [&](const char *where) {
        const QRect r = w.inCentral();
        EXPECT_EQ(r.x(), 0) << where;
        EXPECT_EQ(r.width(), w.central()->width()) << where;
        EXPECT_EQ(r.bottom(), w.central()->height() - 1) << where;
    };
    expectAnchored("first open");
    const int height = w.overlay->height();
    w.window.resize(1400, 900);
    settle();
    expectAnchored("after a resize");
    EXPECT_EQ(w.overlay->height(), height); // the height is the user's, not the window's
    // A window too short for that height: held under the ribbon.
    w.window.resize(1000, 500);
    settle();
    expectAnchored("in a short window");
    EXPECT_LE(w.overlay->height(), w.maxHeight());
    EXPECT_GE(w.inCentral().y(), w.ribbon()->geometry().bottom() + 1);
}

TEST(TimingOverlay, MenuItemAndCloseButtonStayInStep)
{
    OverlayWindow w;
    ASSERT_TRUE(w.ready());
    ASSERT_NE(w.closeButton(), nullptr);
    EXPECT_TRUE(w.toggle->isCheckable());
    EXPECT_FALSE(w.overlay->isVisible());
    EXPECT_FALSE(w.toggle->isChecked());

    w.toggle->trigger();
    settle();
    EXPECT_TRUE(w.overlay->isVisible());
    EXPECT_TRUE(w.toggle->isChecked());

    w.closeButton()->click();
    settle();
    EXPECT_FALSE(w.overlay->isVisible());
    EXPECT_FALSE(w.toggle->isChecked());

    w.toggle->trigger();
    settle();
    EXPECT_TRUE(w.overlay->isVisible());
    w.toggle->trigger();
    settle();
    EXPECT_FALSE(w.overlay->isVisible());
    EXPECT_FALSE(w.toggle->isChecked());
}

TEST(TimingOverlay, DraggingTheTopEdgeResizesWithinTheLimits)
{
    QSettings("Atlas", "Atlas").remove(kHeightKey);
    OverlayWindow w;
    ASSERT_TRUE(w.ready());
    w.toggle->trigger();
    settle();
    ASSERT_NE(w.grip(), nullptr);
    EXPECT_EQ(w.grip()->cursor().shape(), Qt::SizeVerCursor);

    const int start = w.overlay->height();
    w.drag(-50); // up: taller
    EXPECT_EQ(w.overlay->height(), start + 50);
    w.drag(30); // down: shorter
    EXPECT_EQ(w.overlay->height(), start + 20);

    w.drag(10000); // far down: the minimum
    EXPECT_EQ(w.overlay->height(), 150);
    w.drag(-10000); // far up: up to the ribbon, never over it or the menu
    EXPECT_EQ(w.overlay->height(), w.maxHeight());
    EXPECT_EQ(w.inCentral().y(), w.ribbon()->geometry().bottom() + 1);
    EXPECT_EQ(w.inCentral().bottom(), w.central()->height() - 1);
}

TEST(TimingOverlay, HeightIsRememberedBetweenRuns)
{
    QSettings("Atlas", "Atlas").remove(kHeightKey);
    int dragged = 0;
    {
        OverlayWindow w;
        ASSERT_TRUE(w.ready());
        w.toggle->trigger();
        settle();
        ASSERT_NE(w.grip(), nullptr);
        w.drag(-37);
        dragged = w.overlay->height();
        EXPECT_EQ(QSettings("Atlas", "Atlas").value(kHeightKey).toInt(), dragged);
    }
    {
        OverlayWindow w;
        ASSERT_TRUE(w.ready());
        w.toggle->trigger();
        settle();
        EXPECT_EQ(w.overlay->height(), dragged);
    }
    QSettings("Atlas", "Atlas").remove(kHeightKey);
}

// What is inside still works from the overlay: the panel is the same one.
TEST(TimingOverlay, HoldsTheTimingGridPanel)
{
    OverlayWindow w;
    ASSERT_TRUE(w.ready());
    auto *panel = w.window.findChild<TimingGridPanel *>("timingGridPanel");
    ASSERT_NE(panel, nullptr);
    EXPECT_TRUE(w.overlay->isAncestorOf(panel));
    w.toggle->trigger();
    settle();
    EXPECT_TRUE(panel->isVisible());
    EXPECT_GT(panel->height(), 0);
}
