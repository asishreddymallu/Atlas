/**
 * @file circuit_view_zoom_test.cpp
 * @brief final/polish: the processor diagram's zoom - limits, Fit, the
 *        anchor under the cursor, the buttons and keys, the zoom surviving a
 *        Step, Run and Undo, and the stage headers staying on their columns.
 */
#include "processor/processor_manager.h"
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"
#include "ui/processor_tab/circuit_scene.h"
#include "ui/processor_tab/circuit_view.h"
#include "ui/processor_tab/processor_designs/components/pipeline_reg.h"
#include "ui/processor_tab/processor_tab.h"
#include "ui/timing_grid/timing_grid.h"
#include "assembler/assembler.h"
#include "utils/utils.h"

#include <QApplication>
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QToolButton>
#include <QWheelEvent>

#include <gtest/gtest.h>

#include <memory>

using namespace Atlas;

namespace
{
std::unique_ptr<CircuitScene> loadDesign(const char *file)
{
    auto scene = std::make_unique<CircuitScene>();
    scene->loadScene(QStringLiteral("../resources/circuit_designs/") + file);
    return scene;
}

struct ViewBench
{
    std::unique_ptr<CircuitScene> scene = loadDesign("H_F_Processor.json");
    CircuitView                   view;
    ViewBench()
    {
        view.resize(900, 600);
        view.show();
        view.showScene(scene.get());
        QApplication::processEvents();
    }
};

void sendCtrlKey(QWidget *to, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::ControlModifier);
    QApplication::sendEvent(to, &press);
}
} // namespace

TEST(CircuitViewZoom, ZoomIsClampedTo25And400Percent)
{
    EXPECT_DOUBLE_EQ(CircuitView::clampZoom(0.01), 0.25);
    EXPECT_DOUBLE_EQ(CircuitView::clampZoom(10.0), 4.0);
    EXPECT_DOUBLE_EQ(CircuitView::clampZoom(1.5), 1.5);
    ViewBench b;
    b.view.setZoom(10.0);
    EXPECT_DOUBLE_EQ(b.view.zoom(), 4.0);
    b.view.zoomIn();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 4.0);
    b.view.setZoom(0.01);
    EXPECT_DOUBLE_EQ(b.view.zoom(), 0.25);
    b.view.zoomOut();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 0.25);
    b.view.setZoom(1.0);
    b.view.zoomIn();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 1.25);
    EXPECT_FALSE(b.view.isFit());
}

TEST(CircuitViewZoom, FitShowsTheWholeDiagramAndRefitsOnResize)
{
    ViewBench b;
    EXPECT_TRUE(b.view.isFit());
    const QRectF r = b.view.fitRect();
    const QSize  v = b.view.viewport()->size();
    EXPECT_NEAR(b.view.zoom(), std::min(v.width() / r.width(), v.height() / r.height()), 1e-9);
    // The whole diagram is inside the viewport.
    const QRect shown = b.view.mapFromScene(r).boundingRect();
    EXPECT_TRUE(b.view.viewport()->rect().adjusted(-2, -2, 2, 2).contains(shown));
    // A resize refits.
    b.view.resize(1300, 800);
    QApplication::processEvents();
    const QSize v2 = b.view.viewport()->size();
    EXPECT_NEAR(b.view.zoom(), std::min(v2.width() / r.width(), v2.height() / r.height()), 1e-9);
    // Zoomed by hand, a resize keeps the zoom.
    b.view.setZoom(2.0);
    b.view.resize(1000, 700);
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 2.0);
    b.view.zoomFit();
    EXPECT_TRUE(b.view.isFit());
}

TEST(CircuitViewZoom, CtrlWheelZoomsAboutThePointUnderTheCursor)
{
    ViewBench b;
    // From 200 %, where the diagram overflows the view and can be anchored
    // (smaller than the view, Qt centres it).
    b.view.setZoom(2.0);
    QApplication::processEvents();
    const QPoint  cursor(250, 180);
    const QPointF before = b.view.mapToScene(cursor);
    QWheelEvent wheel(QPointF(cursor), b.view.viewport()->mapToGlobal(QPointF(cursor)), QPoint(), QPoint(0, 240),
                      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(b.view.viewport(), &wheel);
    EXPECT_NEAR(b.view.zoom(), 2.0 * 1.15 * 1.15, 1e-9); // two notches
    EXPECT_FALSE(b.view.isFit());
    const QPointF after = b.view.mapToScene(cursor);
    EXPECT_NEAR(after.x(), before.x(), 2.0 / b.view.zoom());
    EXPECT_NEAR(after.y(), before.y(), 2.0 / b.view.zoom());
}

TEST(CircuitViewZoom, TheButtonsZoomAndFit)
{
    ViewBench b;
    auto *in  = b.view.findChild<QToolButton *>(QStringLiteral("circuitZoomIn"));
    auto *out = b.view.findChild<QToolButton *>(QStringLiteral("circuitZoomOut"));
    auto *fit = b.view.findChild<QToolButton *>(QStringLiteral("circuitZoomFit"));
    auto *one = b.view.findChild<QToolButton *>(QStringLiteral("circuitZoom100"));
    ASSERT_TRUE(in && out && fit && one);
    one->click();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 1.0);
    in->click();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 1.25);
    out->click();
    EXPECT_DOUBLE_EQ(b.view.zoom(), 1.0);
    fit->click();
    EXPECT_TRUE(b.view.isFit());
    // At the bottom right of the diagram.
    const QRect area = b.view.viewport()->geometry();
    const QRect btns = b.view.findChild<QWidget *>(QStringLiteral("circuitZoomButtons"))->geometry();
    EXPECT_TRUE(area.contains(btns));
    EXPECT_GT(btns.left(), area.center().x());
    EXPECT_GT(btns.top(), area.center().y());
}

TEST(CircuitViewZoom, StageHeadersStayOnTheirColumnsAtEveryZoom)
{
    ViewBench b;
    QList<QRectF> regs;
    for (QGraphicsItem *item : b.scene->items())
        if (auto *r = dynamic_cast<PipelineReg *>(item))
            regs << r->mapRectToScene(r->boundingRect());
    std::sort(regs.begin(), regs.end(), [](const QRectF &a, const QRectF &c) { return a.left() < c.left(); });
    ASSERT_EQ(regs.size(), 4);
    const auto headers = b.scene->stageHeaders();
    ASSERT_EQ(headers.size(), 5);
    for (double z : {0.25, 1.0, 4.0})
    {
        b.view.setZoom(z);
        QApplication::processEvents();
        for (int s = 0; s < 5; ++s)
        {
            const int centre = b.view.mapFromScene(headers[s].second.center()).x();
            if (s > 0)
                EXPECT_GT(centre, b.view.mapFromScene(regs[s - 1].center()).x()) << z << " " << s;
            if (s < 4)
                EXPECT_LT(centre, b.view.mapFromScene(regs[s].center()).x()) << z << " " << s;
        }
    }
}

TEST(CircuitViewZoom, TheZoomSurvivesAStepRunAndUndoButNotAProcessorChange)
{
    MainWindow window;
    window.resize(1400, 900);
    window.show();
    auto *view    = window.findChild<ProcessorTab *>()->findChild<CircuitView *>();
    auto *manager = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"))->manager();
    ASSERT_TRUE(view && manager);
    window.setCurrentTab(MainWindow::ProcessorTabIndex);
    window.processorChanged(ProcessorType::RV5Stage_H_F);
    QApplication::processEvents();
    EXPECT_TRUE(view->isFit());

    view->setZoom(2.0);
    setupVmStateDirectory();
    manager->setStepDelay(0);
    manager->loadProgram(assemble("../examples/scheduler/load_use_original.s"));
    manager->setBreakpoints({}); // known_issues 86
    manager->step();
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(view->zoom(), 2.0) << "after a Step";
    manager->undo();
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(view->zoom(), 2.0) << "after an Undo";
    manager->run();
    QApplication::processEvents();
    EXPECT_DOUBLE_EQ(view->zoom(), 2.0) << "after a Run";
    EXPECT_FALSE(view->isFit());

    window.processorChanged(ProcessorType::RVSS);
    QApplication::processEvents();
    EXPECT_TRUE(view->isFit()) << "a new processor starts at Fit";
}

TEST(CircuitViewZoom, TheKeysWorkOnlyWhileTheDiagramHasFocus)
{
    MainWindow window;
    window.show();
    auto *view = window.findChild<ProcessorTab *>()->findChild<CircuitView *>();
    ASSERT_NE(view, nullptr);
    Editor *editor = nullptr;
    for (QPlainTextEdit *p : window.findChild<EditorTab *>()->findChildren<QPlainTextEdit *>())
        if (!p->isReadOnly())
            editor = static_cast<Editor *>(p);
    ASSERT_NE(editor, nullptr);
    view->zoom100();
    // Sent to the editor, Ctrl+plus zooms the editor's text, not the diagram.
    sendCtrlKey(editor, Qt::Key_Plus);
    EXPECT_DOUBLE_EQ(view->zoom(), 1.0);
    // Sent to the diagram, the keys zoom it.
    sendCtrlKey(view, Qt::Key_Plus);
    EXPECT_DOUBLE_EQ(view->zoom(), 1.25);
    sendCtrlKey(view, Qt::Key_Minus);
    EXPECT_DOUBLE_EQ(view->zoom(), 1.0);
    sendCtrlKey(view, Qt::Key_0);
    EXPECT_TRUE(view->isFit());
}
