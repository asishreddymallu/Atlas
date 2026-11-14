/**
 * @file open_clears_breakpoints_test.cpp
 * @brief File > Open drops the breakpoints; nothing else does.
 *
 * A breakpoint is a line number in the file that was open. Opening a
 * different one makes it meaningless, so the open path clears them.
 *
 * It is the OPEN path specifically, not Editor::setPlainText: the Expanded
 * view and a reload both set text and have always left the numbers alone.
 * `BreakpointKeysDocumentSwap.LoadingAFileLeavesTheNumbersWhereTheyWere`
 * still pins that, unchanged.
 *
 * The File > Open handler is a lambda around a modal
 * QFileDialog::getOpenFileName, which a test cannot drive. The part after
 * the dialog is `EditorTab::loadFileContent()`, which is what the lambda
 * calls and what these cases call.
 *
 * ONE CASE HERE IS NOT A GUARD. `CancellingTheDialogClearsNothing`
 * documents the contract and cannot fail under any change to the
 * implementation - no mutation of the editor or the tab makes it go red,
 * which was checked. Cancel is covered by structure instead: the clear sits
 * inside the handler's `!filename.isEmpty()` branch, so cancelling cannot
 * reach it. It is not counted as a negative-control target anywhere.
 */
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QPlainTextEdit>
#include <QEvent>
#include <QWidget>

using namespace Kites;

namespace
{
struct Tab
{
    MainWindow window;
    EditorTab *tab    {nullptr};
    Editor    *editor {nullptr};

    Tab()
    {
        window.resize(1200, 800);
        window.show();
        QApplication::processEvents();
        tab = window.findChild<EditorTab *>();
        for (QPlainTextEdit *pane : tab->findChildren<QPlainTextEdit *>())
            if (!pane->isReadOnly())
                editor = static_cast<Editor *>(pane);
    }

    QList<int> breakpoints() const
    {
        QList<int> out;
        for (uint64_t line : editor->getBreakpoints())
            out << int(line);
        return out;
    }
};

/// The gutter widget that draws the dots.
QWidget *breakpointGutter(Editor *editor)
{
    for (QWidget *child : editor->findChildren<QWidget *>())
        if (QString(child->metaObject()->className()).contains("BreakPointGutter") ||
            child->objectName() == "breakPointGutter")
            return child;
    return nullptr;
}
} // namespace

TEST(OpenClearsBreakpoints, OpeningAFileDropsThem)
{
    Tab bench;
    bench.editor->setPlainText("one\ntwo\nthree\nfour");
    bench.editor->toggleBreakpointAtLine(2);
    bench.editor->toggleBreakpointAtLine(4);
    ASSERT_EQ(bench.breakpoints(), (QList<int>{2, 4}));

    bench.tab->loadFileContent("a different\nfile entirely\n");
    QApplication::processEvents();

    EXPECT_EQ(bench.breakpoints(), (QList<int>{})) << "opening a file kept the breakpoints";
    EXPECT_EQ(bench.editor->toPlainText(), QString("a different\nfile entirely\n"))
        << "opening a file did not load it";
}

TEST(OpenClearsBreakpoints, TheDotStopsBeingDrawn)
{
    // The dots are drawn by a gutter column, a sibling of the viewport that
    // does not repaint just because the editor does - so clearing has to ask
    // it to. Read from the pixels, and from BOTH sides: the case first
    // checks the dot is there, so "no ink" cannot pass for the wrong reason.
    Tab bench;
    bench.editor->setPlainText("one\ntwo\nthree");
    bench.editor->toggleBreakpointAtLine(2);
    QApplication::processEvents();

    QWidget *gutter = breakpointGutter(bench.editor);
    ASSERT_NE(gutter, nullptr) << "no breakpoint gutter found";

    auto inkPixels = [&]
    {
        const QImage shot   = gutter->grab().toImage();
        const QRgb   ground = shot.pixel(0, 0);
        int          ink    = 0;
        for (int y = 0; y < shot.height(); ++y)
            for (int x = 0; x < shot.width(); ++x)
                if (shot.pixel(x, y) != ground)
                    ++ink;
        return ink;
    };

    const int withDot = inkPixels();
    ASSERT_GT(withDot, 0) << "the gutter drew no dot for a breakpoint, so this case "
                             "could not tell a repaint from a blank gutter";

    // Count the paint events the gutter is actually sent. grab() repaints
    // on demand, so the pixels alone cannot tell "it was asked to repaint"
    // from "it was repainted because something looked at it".
    struct PaintCounter : QObject
    {
        int count {0};
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint)
                ++count;
            return false;
        }
    } counter;
    gutter->installEventFilter(&counter);

    bench.tab->loadFileContent("x\ny\n");
    QApplication::processEvents();
    gutter->removeEventFilter(&counter);

    EXPECT_GT(counter.count, 0) << "the gutter was never repainted";
    EXPECT_EQ(inkPixels(), 0)
        << "the gutter still draws a dot after the breakpoints went (was " << withDot
        << " pixels)";
}

TEST(OpenClearsBreakpoints, CancellingTheDialogClearsNothing)
{
    // NOT A GUARD - see the file header. QFileDialog::getOpenFileName is
    // static and modal, so the cancel path cannot be driven from a test, and
    // nothing in the implementation can make this case fail. It writes the
    // contract down: the clear lives inside loadFileContent(), which the
    // handler only reaches when a filename came back, so not calling it -
    // which is exactly what cancelling does - leaves everything alone.
    Tab bench;
    bench.editor->setPlainText("one\ntwo\nthree");
    bench.editor->toggleBreakpointAtLine(3);
    ASSERT_EQ(bench.breakpoints(), (QList<int>{3}));

    QApplication::processEvents();

    EXPECT_EQ(bench.breakpoints(), (QList<int>{3}))
        << "something cleared the breakpoints without a file being opened";
    EXPECT_EQ(bench.editor->toPlainText(), QString("one\ntwo\nthree"));
}

TEST(OpenClearsBreakpoints, SetPlainTextOnItsOwnStillLeavesThemAlone)
{
    // The other half of the rule, stated here as well as in
    // BreakpointKeysDocumentSwap: setting the text is not opening a file.
    Tab bench;
    bench.editor->setPlainText("one\ntwo\nthree\nfour");
    bench.editor->toggleBreakpointAtLine(2);
    bench.editor->toggleBreakpointAtLine(4);

    bench.editor->setPlainText("shorter\nfile");
    QApplication::processEvents();
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4}))
        << "setPlainText started clearing breakpoints";

    bench.tab->setRawText("another\none\nhere");
    QApplication::processEvents();
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4}))
        << "setRawText started clearing breakpoints";
}
