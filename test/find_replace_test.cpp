/**
 * @file find_replace_test.cpp
 * @brief The find bar, replace and go-to-line, driven as a real editor.
 *
 * The engine has its own table in find_engine_test.cpp. What is here is
 * everything only the widget can answer: that the overlay does not resize
 * the viewport, that finding never touches the undo stack, that the
 * highlights compose with the diagnostics instead of replacing them, that
 * Replace All is one undo step, and what happens to breakpoints when a
 * replacement changes the number of lines.
 */
#include "ui/editor_tab/find_bar.h"
#include "ui/editor_tab/find_engine.h"
#include "ui/editor_tab/goto_line_bar.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"

#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QElapsedTimer>
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolButton>
#include <random>

using namespace Kites;

namespace
{
void sendKey(QWidget *target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString &text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

/// A shown, writable editor. The debounce is 150 ms, so anything that types
/// into the find field has to let it elapse.
struct Bench
{
    Editor editor;

    Bench()
    {
        editor.resize(700, 400);
        editor.show();
        QApplication::processEvents();
    }

    void place(const QString &text, int anchor = 0, int position = 0)
    {
        editor.setPlainText(text);
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);
        QApplication::processEvents();
    }

    QLineEdit *findInput() const
    {
        return editor.findBar() ? editor.findBar()->findChild<QLineEdit *>("findInput")
                                : nullptr;
    }
    QLineEdit *replaceInput() const
    {
        return editor.findBar() ? editor.findBar()->findChild<QLineEdit *>("replaceInput")
                                : nullptr;
    }

    /// Types into the find field and waits out the debounce.
    void search(const QString &pattern)
    {
        ASSERT_NE(findInput(), nullptr);
        findInput()->setText(pattern);
        settle(260);
    }

    static void settle(int ms)
    {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < ms)
            QApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    QString selected() const { return editor.textCursor().selectedText(); }
    int     matchCount() const { return int(editor.findResult().matches.size()); }
    QString status() const
    {
        return editor.findBar() ? editor.findBar()->status() : QString();
    }
};
} // namespace

// ═════════════════════ the overlay does not move the text ═════════════════

TEST(FindBarGeometry, TheViewportIsTheSameSizeOpenAndClosed)
{
    Bench bench;
    bench.place("one\ntwo\nthree");
    const QSize closed = bench.editor.viewport()->size();

    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    ASSERT_NE(bench.editor.findBar(), nullptr);
    ASSERT_FALSE(bench.editor.findBar()->isHidden());
    EXPECT_EQ(bench.editor.viewport()->size(), closed)
        << "opening the find bar resized the viewport";

    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier); // replace row too
    EXPECT_EQ(bench.editor.viewport()->size(), closed)
        << "showing the replace row resized the viewport";

    sendKey(&bench.editor, Qt::Key_Escape);
    EXPECT_EQ(bench.editor.viewport()->size(), closed);
}

TEST(FindBarGeometry, TheBarStaysInsideTheViewportAtEveryWidth)
{
    Bench bench;
    bench.place("some text to search");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    FindBar *bar = bench.editor.findBar();
    ASSERT_NE(bar, nullptr);

    for (int width : {849, 900, 1000, 1200, 1366, 1600, 1920})
    {
        bench.editor.resize(width, 400);
        QApplication::processEvents();
        const QWidget *viewport = bench.editor.viewport();
        EXPECT_LE(bar->width(), viewport->width() - 16)
            << "wider than the viewport minus 16 at " << width;
        EXPECT_GE(bar->x(), 0) << "off the left edge at " << width;
        EXPECT_LE(bar->x() + bar->width(), viewport->width())
            << "off the right edge at " << width;
        EXPECT_GE(bar->y(), 0);
    }
}

// ═════════════════════════ opening and navigating ═════════════════════════

TEST(FindKeys, CtrlFOpensAndPrefillsFromASingleLineSelectionOnly)
{
    Bench bench;
    bench.place("alpha beta\ngamma delta", 0, 5); // "alpha"
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    ASSERT_NE(bench.findInput(), nullptr);
    EXPECT_EQ(bench.findInput()->text(), QString("alpha"));
    EXPECT_EQ(bench.findInput()->selectedText(), QString("alpha"))
        << "the field should have everything selected, ready to be typed over";

    // A selection spanning lines is a block of text, not a search term.
    sendKey(&bench.editor, Qt::Key_Escape);
    bench.place("alpha beta\ngamma delta", 0, 15);
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    EXPECT_EQ(bench.findInput()->text(), QString("alpha"))
        << "a multi-line selection overwrote the search term";
}

TEST(FindKeys, EnterAndF3WalkForwardAndBackwardAndWrap)
{
    Bench bench;
    bench.place("x..x..x");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("x");
    ASSERT_EQ(bench.matchCount(), 3);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 0);
    EXPECT_EQ(bench.status(), QString("1 of 3"));

    sendKey(bench.findInput(), Qt::Key_Return);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 1);
    sendKey(&bench.editor, Qt::Key_F3);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 2);
    sendKey(&bench.editor, Qt::Key_F3);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 0) << "forward did not wrap";

    sendKey(&bench.editor, Qt::Key_F3, Qt::ShiftModifier);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 2) << "backward did not wrap";
    sendKey(bench.findInput(), Qt::Key_Return, Qt::ShiftModifier);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 1);
}

TEST(FindKeys, F3WithTheBarClosedOpensItAndGoesOn)
{
    Bench bench;
    bench.place("x..x..x");

    // With no term at all it just opens.
    sendKey(&bench.editor, Qt::Key_F3);
    ASSERT_NE(bench.editor.findBar(), nullptr);
    EXPECT_FALSE(bench.editor.findBar()->isHidden());
    EXPECT_EQ(bench.editor.currentMatchIndex(), -1);

    // With a term, closing and pressing F3 again carries on.
    bench.search("x");
    ASSERT_EQ(bench.matchCount(), 3);
    const int before = bench.editor.currentMatchIndex();
    sendKey(&bench.editor, Qt::Key_Escape);
    ASSERT_TRUE(bench.editor.findBar()->isHidden());
    sendKey(&bench.editor, Qt::Key_F3);
    EXPECT_FALSE(bench.editor.findBar()->isHidden()) << "F3 did not reopen the bar";
    EXPECT_NE(bench.editor.currentMatchIndex(), -1) << "F3 did not go to a match";
    EXPECT_NE(bench.editor.currentMatchIndex(), before)
        << "F3 reopened on the same match instead of the next";
}

TEST(FindKeys, EscClosesLeavesTheMatchSelectedAndReturnsFocus)
{
    Bench bench;
    bench.place("alpha beta alpha");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("beta");
    ASSERT_EQ(bench.matchCount(), 1);
    EXPECT_EQ(bench.selected(), QString("beta"));

    sendKey(bench.findInput(), Qt::Key_Escape);
    EXPECT_TRUE(bench.editor.findBar()->isHidden()) << "Esc did not close the bar";
    EXPECT_EQ(bench.selected(), QString("beta"))
        << "Esc lost the match that was selected";
    EXPECT_TRUE(bench.editor.hasFocus()) << "focus did not come back to the editor";

    // And Esc in the EDITOR with the bar open closes it too.
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    ASSERT_FALSE(bench.editor.findBar()->isHidden());
    sendKey(&bench.editor, Qt::Key_Escape);
    EXPECT_TRUE(bench.editor.findBar()->isHidden());
}

TEST(FindKeys, AltCWRToggleTheOptionsFromInsideTheBar)
{
    Bench bench;
    bench.place("Alpha alpha");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    FindBar *bar = bench.editor.findBar();
    bench.search("alpha");
    EXPECT_EQ(bench.matchCount(), 2) << "the default should be case-insensitive";

    sendKey(bench.findInput(), Qt::Key_C, Qt::AltModifier);
    Bench::settle(260);
    EXPECT_TRUE(bar->options().caseSensitive);
    EXPECT_EQ(bench.matchCount(), 1) << "Alt+C did not take effect";

    sendKey(bench.findInput(), Qt::Key_W, Qt::AltModifier);
    EXPECT_TRUE(bar->options().wholeWord);
    sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier);
    EXPECT_TRUE(bar->options().regex);

    // And again turns them off.
    sendKey(bench.findInput(), Qt::Key_C, Qt::AltModifier);
    EXPECT_FALSE(bar->options().caseSensitive);
}

TEST(FindIncremental, TypingSelectsTheFirstMatchFromWhereTheBarWasOpened)
{
    Bench bench;
    //            0123456789...
    bench.place("aa bb aa bb aa", 6, 6); // caret just before the second "aa"
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("aa");
    ASSERT_EQ(bench.matchCount(), 3);
    EXPECT_EQ(bench.editor.currentMatchIndex(), 1)
        << "the search did not start from the caret";

    // Growing the pattern searches from the SAME origin, not from the match
    // the previous keystroke happened to select. "aa b" matches at 0 and 6;
    // from the origin (6) that is index 1, while an implementation that
    // resumed from the end of the previous selection would have wrapped
    // round to index 0.
    bench.search("aa b");
    EXPECT_EQ(bench.editor.currentMatchIndex(), 1)
        << "incremental search crawled forward as the pattern grew";
}

TEST(FindIncremental, NoAmountOfFindingTouchesTheUndoStack)
{
    Bench bench;
    bench.place("alpha beta alpha beta");
    const int  revision = int(bench.editor.document()->revision());
    const bool undoWas  = bench.editor.document()->isUndoAvailable();
    const bool redoWas  = bench.editor.document()->isRedoAvailable();

    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("alpha");
    for (int i = 0; i < 10; ++i)
    {
        sendKey(&bench.editor, Qt::Key_F3);
        sendKey(&bench.editor, Qt::Key_F3, Qt::ShiftModifier);
    }
    bench.search("beta");
    sendKey(&bench.editor, Qt::Key_Escape);

    EXPECT_EQ(int(bench.editor.document()->revision()), revision)
        << "finding changed the document revision";
    EXPECT_EQ(bench.editor.document()->isUndoAvailable(), undoWas);
    EXPECT_EQ(bench.editor.document()->isRedoAvailable(), redoWas);
}

TEST(FindStatus, CountsNoResultsAndAnInvalidRegex)
{
    Bench bench;
    bench.place("one two three");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);

    bench.search("two");
    EXPECT_EQ(bench.status(), QString("1 of 1"));

    bench.search("zzz");
    EXPECT_EQ(bench.status(), QString("No results"));
    EXPECT_EQ(bench.editor.currentMatchIndex(), -1);

    sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier); // regex on
    bench.search("a(");
    EXPECT_TRUE(bench.status().startsWith("Invalid regex:"))
        << "status was: " << bench.status().toStdString();
    EXPECT_EQ(bench.matchCount(), 0);

    bench.search("");
    EXPECT_EQ(bench.status(), QString()) << "an empty box should say nothing";
}

TEST(FindHighlights, DiagnosticsSurviveDuringAndAfterASearch)
{
    Bench bench;
    bench.place("alpha beta\ngamma alpha");

    QTextEdit::ExtraSelection squiggle;
    squiggle.cursor = QTextCursor(bench.editor.document());
    squiggle.cursor.setPosition(0);
    squiggle.cursor.setPosition(5, QTextCursor::KeepAnchor);
    squiggle.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
    bench.editor.setDiagnosticSelections({squiggle});

    auto squiggles = [&]
    {
        int n = 0;
        for (const QTextEdit::ExtraSelection &s : bench.editor.extraSelections())
            if (s.format.underlineStyle() != QTextCharFormat::NoUnderline)
                ++n;
        return n;
    };
    ASSERT_EQ(squiggles(), 1);

    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("alpha");
    ASSERT_EQ(bench.matchCount(), 2);
    EXPECT_GT(bench.editor.extraSelections().size(), 1)
        << "the match highlights did not reach the editor";
    EXPECT_EQ(squiggles(), 1) << "the search wiped the diagnostics";

    sendKey(&bench.editor, Qt::Key_Escape);
    EXPECT_EQ(squiggles(), 1) << "closing the bar wiped the diagnostics";
}

TEST(FindHighlights, OnlyVisibleMatchesGetASelectionButTheCountIsTheWholeDocument)
{
    Bench bench;
    QStringList lines;
    for (int i = 0; i < 2000; ++i)
        lines << "needle here";
    bench.place(lines.join('\n'));

    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("needle");

    EXPECT_EQ(bench.matchCount(), 2000) << "the count should cover the whole document";
    EXPECT_EQ(bench.status(), QString("1 of 2000"));
    EXPECT_LT(bench.editor.extraSelections().size(), 200)
        << "every match in a 2000 line document got a selection";
    EXPECT_GT(bench.editor.extraSelections().size(), 0)
        << "nothing on screen was highlighted at all";
}
