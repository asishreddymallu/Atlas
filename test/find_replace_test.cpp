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

// ══════════════════════════════ replace ═══════════════════════════════════

namespace
{
QList<int> breakpointsOf(const Editor &editor)
{
    QList<int> out;
    for (uint64_t line : editor.getBreakpoints())
        out << int(line);
    return out;
}
} // namespace

TEST(ReplaceKeys, CtrlHOpensTheReplaceRowWithFocusInTheFindField)
{
    Bench bench;
    bench.place("alpha beta alpha");
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    FindBar *bar = bench.editor.findBar();
    ASSERT_NE(bar, nullptr);
    EXPECT_FALSE(bar->isHidden());
    EXPECT_TRUE(bar->isReplaceVisible()) << "Ctrl+H did not show the replace row";
    ASSERT_NE(bench.findInput(), nullptr);
    EXPECT_TRUE(bench.findInput()->hasFocus()) << "focus should start in the find field";

    // Ctrl+F on its own does not show the replace row.
    sendKey(&bench.editor, Qt::Key_Escape);
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    EXPECT_FALSE(bar->isReplaceVisible());
}

TEST(ReplaceOne, ReplacesTheCurrentMatchInOneUndoStepAndMovesOn)
{
    Bench bench;
    bench.place("aa bb aa bb aa");
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("aa");
    ASSERT_EQ(bench.matchCount(), 3);
    bench.replaceInput()->setText("ZZ");
    // Enter means Replace only while the replace FIELD has focus; in the
    // find field it means "next". The bar checks that, so the case has to
    // put the focus where a user would.
    bench.replaceInput()->setFocus();
    QApplication::processEvents();

    sendKey(bench.replaceInput(), Qt::Key_Return);
    EXPECT_EQ(bench.editor.toPlainText(), QString("ZZ bb aa bb aa"));
    EXPECT_EQ(bench.editor.currentMatchIndex(), 0)
        << "after replacing, the next match should be current";

    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("aa bb aa bb aa"))
        << "one Ctrl+Z did not undo one replace";
}

TEST(ReplaceAll, IsExactlyOneUndoStepAndSaysHowMany)
{
    Bench bench;
    const QString original = "aa bb aa bb aa";
    bench.place(original);
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("aa");
    bench.replaceInput()->setText("LONGER");

    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("LONGER bb LONGER bb LONGER"));
    EXPECT_EQ(bench.status(), QString("Replaced 3"));

    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), original)
        << "Replace All was more than one undo step";
}

TEST(ReplaceAll, CaretReplaceCommentsEveryLineAndTerminates)
{
    Bench bench;
    const QString original = "addi t0, x0, 5\n\nadd t1, t0, t0";
    bench.place(original);
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier); // regex
    bench.search("^");
    bench.replaceInput()->setText("# ");

    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("# addi t0, x0, 5\n# \n# add t1, t0, t0"));
    EXPECT_EQ(bench.status(), QString("Replaced 3"));

    sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), original) << "not one undo step";
}

TEST(ReplaceAll, UsesCaptureGroupsInRegexModeAndIsLiteralOtherwise)
{
    Bench bench;
    bench.place("a1 a2 a3");
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier);
    bench.search("a(\\d)");
    bench.replaceInput()->setText("<$1>");
    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("<1> <2> <3>"));

    // Plain mode leaves $1 alone.
    bench.place("a1 a2");
    sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier); // regex off
    bench.search("a1");
    bench.replaceInput()->setText("<$1>");
    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("<$1> a2"));
}

TEST(ReplaceReadOnly, AReadOnlyPaneRefusesToReplace)
{
    Bench bench;
    const QString original = "aa bb aa";
    bench.place(original);
    bench.editor.setReadOnly(true);

    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("aa");
    ASSERT_EQ(bench.matchCount(), 2) << "find should still work in a read-only pane";
    ASSERT_NE(bench.replaceInput(), nullptr);
    EXPECT_FALSE(bench.replaceInput()->isEnabled()) << "the replace field is still live";

    // Ctrl+Alt+Enter from the find field still reaches replaceAll, so the
    // guard in the editor is what has to refuse - not just a disabled
    // button.
    sendKey(bench.findInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), original)
        << "Replace All changed a read-only pane";

    // And the replace buttons cannot be reached at all.
    for (const char *name : {"replaceOneButton", "replaceAllButton"})
    {
        auto *button = bench.editor.findBar()->findChild<QToolButton *>(name);
        ASSERT_NE(button, nullptr) << name;
        EXPECT_FALSE(button->isEnabled()) << name << " is still enabled";
    }
}

// ══════════════════════ replace and breakpoints ═══════════════════════════

TEST(ReplaceBreakpoints, AReplaceThatKeepsTheLineCountLeavesThemAlone)
{
    Bench bench;
    bench.place("aa one\nbb two\ncc three\ndd four");
    bench.editor.setBreakpoints({});
    for (int line : {2, 4})
        bench.editor.toggleBreakpointAtLine(line);

    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("o");
    bench.replaceInput()->setText("0");
    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);

    EXPECT_EQ(bench.editor.document()->blockCount(), 4);
    EXPECT_EQ(breakpointsOf(bench.editor), (QList<int>{2, 4}))
        << "a same-line-count replace moved a breakpoint";
}

TEST(ReplaceBreakpoints, TheRuleWhenAReplacementChangesTheNumberOfLines)
{
    // PINNED, not left undefined. A replacement that splits a line leaves
    // the breakpoint on the LAST of the lines that come out of it, and
    // everything below shifts by the change in line count. A breakpoint
    // whose line is swallowed whole by a multi-line removal is dropped.
    // This is Qt moving the anchors, not a LineMap - Replace All is not an
    // EditorCommands edit.
    {
        Bench bench;
        bench.place("l1\nl2\nl3\nl4");
        bench.editor.setBreakpoints({});
        for (int line : {2, 4})
            bench.editor.toggleBreakpointAtLine(line);

        sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
        bench.search("l2");
        bench.replaceInput()->setText("a\nb");
        sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);

        EXPECT_EQ(bench.editor.toPlainText(), QString("l1\na\nb\nl3\nl4"));
        EXPECT_EQ(breakpointsOf(bench.editor), (QList<int>{3, 5}))
            << "a line-adding replace did not follow the pinned rule";
    }
    {
        Bench bench;
        bench.place("l1\nl2\nl3\nl4");
        bench.editor.setBreakpoints({});
        for (int line : {2, 4})
            bench.editor.toggleBreakpointAtLine(line);

        sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
        sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier);
        bench.search("l2\\nl3");
        bench.replaceInput()->setText("X");
        sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);

        EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nX\nl4"));
        EXPECT_EQ(breakpointsOf(bench.editor), (QList<int>{3}))
            << "a line-removing replace did not follow the pinned rule";
    }
}

TEST(ReplaceBreakpoints, AHundredRandomReplacementsKeepThemLegal)
{
    Bench bench;
    QStringList lines;
    for (int i = 0; i < 20; ++i)
        lines << QString("line %1 aa bb cc").arg(i);
    const QString document = lines.join('\n');

    // "\\n" is in the list on purpose: in regex mode it matches the line
    // separators themselves, so a replacement can JOIN lines and land two
    // breakpoints on one. Without it the marked lines are five apart and
    // nothing in the sweep can ever collide - which a negative control
    // proved by disabling the de-duplication and watching this pass.
    const QStringList patterns     = {"aa", "bb", "line", "c", " ", "\\d", "\\n"};
    const QStringList replacements = {"X", "", "a\nb", "longer text", "\n", "YY"};

    std::mt19937 rng(20260116); // fixed, so a failure is reproducible
    std::uniform_int_distribution<int> pickPattern(0, patterns.size() - 1);
    std::uniform_int_distribution<int> pickReplacement(0, replacements.size() - 1);

    for (int round = 0; round < 100; ++round)
    {
        bench.place(document);
        bench.editor.setBreakpoints({});
        for (int line : {1, 5, 10, 15, 20})
            bench.editor.toggleBreakpointAtLine(line);
        const int started = breakpointsOf(bench.editor).size();
        ASSERT_EQ(started, 5);

        sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
        if (round % 2 == 0 && !bench.editor.findBar()->options().regex)
            sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier);
        if (round % 2 == 1 && bench.editor.findBar()->options().regex)
            sendKey(bench.findInput(), Qt::Key_R, Qt::AltModifier);

        bench.search(patterns.at(pickPattern(rng)));
        bench.replaceInput()->setText(replacements.at(pickReplacement(rng)));
        sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);

        const QList<int> after  = breakpointsOf(bench.editor);
        const int        blocks = bench.editor.document()->blockCount();
        ASSERT_LE(after.size(), started) << "the count grew in round " << round;
        for (int i = 0; i < after.size(); ++i)
        {
            ASSERT_GE(after.at(i), 1) << "below line 1 in round " << round;
            ASSERT_LE(after.at(i), blocks) << "past the end in round " << round;
            if (i)
                ASSERT_NE(after.at(i), after.at(i - 1)) << "a duplicate in round " << round;
        }
        sendKey(&bench.editor, Qt::Key_Escape);
    }
}

// ═══════════════════════════ go to line ═══════════════════════════════════

namespace
{
QLineEdit *gotoInput(Editor &editor)
{
    return editor.gotoLineBar() ? editor.gotoLineBar()->findChild<QLineEdit *>("gotoLineInput")
                                : nullptr;
}
int caretLine(const Editor &editor)
{
    return editor.textCursor().blockNumber() + 1;
}
} // namespace

TEST(GotoLine, AcceptsALineAndALineColonColumn)
{
    Bench bench;
    bench.place("one\ntwo\nthree\nfour\nfive");
    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    ASSERT_NE(bench.editor.gotoLineBar(), nullptr);
    ASSERT_FALSE(bench.editor.gotoLineBar()->isHidden());
    ASSERT_NE(gotoInput(bench.editor), nullptr);

    gotoInput(bench.editor)->setText("3");
    sendKey(gotoInput(bench.editor), Qt::Key_Return);
    EXPECT_EQ(caretLine(bench.editor), 3);
    EXPECT_TRUE(bench.editor.gotoLineBar()->isHidden()) << "accepting did not close it";

    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    gotoInput(bench.editor)->setText("4:3");
    sendKey(gotoInput(bench.editor), Qt::Key_Return);
    EXPECT_EQ(caretLine(bench.editor), 4);
    EXPECT_EQ(bench.editor.textCursor().positionInBlock(), 2) << "the column was ignored";
}

TEST(GotoLine, PastTheEndGoesToTheLastLineAndRubbishDoesNothing)
{
    Bench bench;
    bench.place("one\ntwo\nthree");

    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    gotoInput(bench.editor)->setText("99");
    sendKey(gotoInput(bench.editor), Qt::Key_Return);
    EXPECT_EQ(caretLine(bench.editor), 3) << "past the end should clamp to the last line";

    struct Bad { const char *typed; };
    for (const Bad &bad : {Bad{"0"}, Bad{""}, Bad{"abc"}, Bad{"-4"}})
    {
        bench.place("one\ntwo\nthree", 0, 0);
        sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
        gotoInput(bench.editor)->setText(bad.typed);
        sendKey(gotoInput(bench.editor), Qt::Key_Return);
        EXPECT_EQ(caretLine(bench.editor), 1) << "\"" << bad.typed << "\" moved the caret";
        EXPECT_FALSE(bench.editor.gotoLineBar()->isHidden())
            << "\"" << bad.typed << "\" closed the bar";
        EXPECT_TRUE(bench.editor.gotoLineBar()->isErrorState())
            << "\"" << bad.typed << "\" did not put the field in an error state";
        sendKey(gotoInput(bench.editor), Qt::Key_Escape);
    }
}

TEST(GotoLine, EscClosesAndOnlyOneOverlayIsOpenAtATime)
{
    Bench bench;
    bench.place("one\ntwo\nthree");

    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    ASSERT_FALSE(bench.editor.gotoLineBar()->isHidden());
    sendKey(gotoInput(bench.editor), Qt::Key_Escape);
    EXPECT_TRUE(bench.editor.gotoLineBar()->isHidden());
    EXPECT_TRUE(bench.editor.hasFocus()) << "focus did not come back to the editor";

    // Find then goto: the find bar closes.
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    ASSERT_FALSE(bench.editor.findBar()->isHidden());
    sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
    EXPECT_TRUE(bench.editor.findBar()->isHidden()) << "both overlays were open";
    EXPECT_FALSE(bench.editor.gotoLineBar()->isHidden());

    // Goto then find: the goto bar closes.
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    EXPECT_TRUE(bench.editor.gotoLineBar()->isHidden()) << "both overlays were open";
    EXPECT_FALSE(bench.editor.findBar()->isHidden());
}

TEST(GotoLine, NeverTouchesTheUndoStack)
{
    Bench bench;
    bench.place("one\ntwo\nthree\nfour");
    const int  revision = int(bench.editor.document()->revision());
    const bool undoWas  = bench.editor.document()->isUndoAvailable();

    for (const char *typed : {"2", "99", "0", "3:2", "nonsense"})
    {
        sendKey(&bench.editor, Qt::Key_G, Qt::ControlModifier);
        gotoInput(bench.editor)->setText(typed);
        sendKey(gotoInput(bench.editor), Qt::Key_Return);
        sendKey(gotoInput(bench.editor), Qt::Key_Escape);
    }

    EXPECT_EQ(int(bench.editor.document()->revision()), revision);
    EXPECT_EQ(bench.editor.document()->isUndoAvailable(), undoWas);
}

TEST(FindShortcutSet, StillExactlySevenApplicationKeySequences)
{
    MainWindow window;
    QStringList sequences;
    for (QAction *action : window.findChildren<QAction *>())
        if (!action->shortcut().isEmpty())
            sequences << action->shortcut().toString();
    EXPECT_EQ(sequences.size(), 7) << "find or replace added an application shortcut";

    int shortcutObjects = 0;
    for (QObject *child : window.findChildren<QObject *>())
        if (QLatin1String(child->metaObject()->className()) == QLatin1String("QShortcut"))
            ++shortcutObjects;
    EXPECT_EQ(shortcutObjects, 0) << "a QShortcut was added";
}

// ═══════════════ acting on a document that changed underneath ═════════════

TEST(FindStaleMatches, ReplaceAllUsesTheCurrentTextNotTheLastSearch)
{
    // The match list is recomputed on a 150 ms debounce. An edit followed
    // IMMEDIATELY by Replace All - no processEvents, no timer flush - must
    // still replace what is in the document now, not where the matches used
    // to be.
    Bench bench;
    bench.place("aaa");
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("a");
    ASSERT_EQ(bench.matchCount(), 3);
    bench.replaceInput()->setText("Z");

    // Two characters in front of everything, so every stale offset is wrong
    // by exactly two.
    QTextCursor cursor = bench.editor.textCursor();
    cursor.setPosition(0);
    cursor.insertText("XY");
    ASSERT_EQ(bench.editor.toPlainText(), QString("XYaaa"));

    sendKey(bench.replaceInput(), Qt::Key_Return, Qt::ControlModifier | Qt::AltModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("XYZZZ"))
        << "Replace All used the offsets from before the edit";
}

TEST(FindStaleMatches, ReplaceOneUsesTheCurrentTextNotTheLastSearch)
{
    Bench bench;
    bench.place("aaa");
    sendKey(&bench.editor, Qt::Key_H, Qt::ControlModifier);
    bench.search("a");
    ASSERT_EQ(bench.matchCount(), 3);
    bench.replaceInput()->setText("Z");
    bench.replaceInput()->setFocus();
    QApplication::processEvents();

    QTextCursor cursor = bench.editor.textCursor();
    cursor.setPosition(0);
    cursor.insertText("XY");
    ASSERT_EQ(bench.editor.toPlainText(), QString("XYaaa"));

    sendKey(bench.replaceInput(), Qt::Key_Return);
    EXPECT_EQ(bench.editor.toPlainText(), QString("XYZaa"))
        << "Replace One used an offset from before the edit";
}

TEST(FindStaleMatches, NextUsesTheCurrentTextNotTheLastSearch)
{
    Bench bench;
    bench.place("a..a..a");
    sendKey(&bench.editor, Qt::Key_F, Qt::ControlModifier);
    bench.search("a");
    ASSERT_EQ(bench.matchCount(), 3);

    QTextCursor cursor = bench.editor.textCursor();
    cursor.setPosition(0);
    cursor.insertText("XY");          // "XYa..a..a": matches now at 2, 5, 8
    ASSERT_EQ(bench.editor.toPlainText(), QString("XYa..a..a"));

    sendKey(&bench.editor, Qt::Key_F3);
    const QTextCursor after = bench.editor.textCursor();
    EXPECT_EQ(after.selectedText(), QString("a")) << "Next landed on something else";
    EXPECT_TRUE(after.selectionStart() == 2 || after.selectionStart() == 5 ||
                after.selectionStart() == 8)
        << "Next landed at " << after.selectionStart()
        << ", which is not where a match is in the current text";
}

// ═══════════════════ the bar fits the pane it is drawn in ════════════════

/**
 * @brief No child of the find bar may sit on top of the input.
 *
 * kites_test CAN lay the bar out: it is a plain widget in a real layout
 * and openFind() builds it, so the geometry below is the geometry the
 * application gets. What it cannot do is prove the bar is OPAQUE - that
 * needs pixels, and it is checked in the out-of-tree harness instead.
 *
 * The widths are the ones the bar is actually squeezed at. 180 is below
 * anything the application produces and is here to prove the fallback
 * holds rather than merely that the common case does.
 */
TEST(FindBarGeometry, NothingSitsOnTopOfTheInputAtAnyWidth)
{
    Editor editor;
    editor.resize(900, 400);
    editor.show();
    QApplication::processEvents();
    editor.setPlainText("lw t0, 4(sp)\nsw t1, 8(sp)\n");
    editor.openFind(false);
    QApplication::processEvents();

    FindBar *bar = editor.findBar();
    ASSERT_NE(bar, nullptr);
    auto *input = bar->findChild<QLineEdit *>("findInput");
    ASSERT_NE(input, nullptr);

    for (int width : {180, 240, 320, 460, 900})
        for (bool withReplace : {false, true})
            for (int zoom : {0, 9, 18})
            {
                editor.resize(width, 400);
                editor.setZoomDelta(zoom);
                editor.openFind(withReplace);
                QApplication::processEvents();

                for (QWidget *child : bar->findChildren<QWidget *>())
                {
                    if (child == input || child->parentWidget() != bar)
                        continue;
                    if (child->objectName().isEmpty() || !child->isVisibleTo(bar))
                        continue;
                    EXPECT_FALSE(child->geometry().intersects(input->geometry()))
                        << child->objectName().toStdString() << " covers the find input at "
                        << width << " px, replace " << (withReplace ? "shown" : "hidden")
                        << ", zoom " << zoom;
                }
                // And the bar itself stays inside the viewport it overlays.
                EXPECT_LE(bar->geometry().right(), editor.viewport()->width())
                    << "the bar hangs off the viewport at " << width << " px";
            }
    editor.setZoomDelta(0);
}

/**
 * @brief Widening puts back what narrowing took away.
 *
 * A one-way fit would leave the bar permanently stripped after a single
 * narrow moment, which a user would see as buttons that never come back.
 */
TEST(FindBarGeometry, WideningRestoresTheButtonsItDropped)
{
    Editor editor;
    editor.resize(900, 400);
    editor.show();
    editor.openFind(false);
    QApplication::processEvents();

    FindBar *bar = editor.findBar();
    ASSERT_NE(bar, nullptr);
    auto *next = bar->findChild<QToolButton *>("findNextButton");
    ASSERT_NE(next, nullptr);

    editor.resize(900, 400);
    QApplication::processEvents();
    const bool wideShowsNext = next->isVisibleTo(bar);

    editor.resize(200, 400);
    QApplication::processEvents();
    EXPECT_FALSE(next->isVisibleTo(bar)) << "the next button survived a 200 px pane";

    editor.resize(900, 400);
    QApplication::processEvents();
    EXPECT_EQ(next->isVisibleTo(bar), wideShowsNext)
        << "the next button did not come back when the pane widened";
}
