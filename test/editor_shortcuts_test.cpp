/**
 * @file editor_shortcuts_test.cpp
 * @brief The VS Code-style editing keys in the Editor tab's Input pane.
 *
 * The transformations live in `EditorCommands`, which knows nothing about a
 * widget, so what follows is a table of strings: document in, document out,
 * caret in, caret out. That is where the edge cases live - the first line,
 * the last line, no trailing newline, an empty document, a selection that
 * stops at column 0, blank lines inside a comment toggle.
 *
 * The widget that reaches for these is tested separately, once it exists.
 */
#include "ui/editor_tab/editor_commands.h"

#include <gtest/gtest.h>

#include <QApplication>

using namespace Kites;
using namespace Kites::EditorCommands;

namespace
{
/// The document after an Edit, so a case can be written as text-in/text-out.
QString applyTo(const QString &text, const Edit &edit)
{
    if (!edit.valid)
        return text;
    QString out = text;
    out.replace(edit.start, edit.length, edit.text);
    return out;
}

/// One row of a command table.
struct Case
{
    const char *name;
    QString     before;
    int         anchor;
    int         position;
    QString     after;
    int         newAnchor;
    int         newPosition;
};

/// Runs a table against one pure command.
template <typename Command>
void runTable(const QList<Case> &cases, Command command)
{
    for (const Case &row : cases)
    {
        const Edit edit = command(row.before, row.anchor, row.position);
        EXPECT_EQ(applyTo(row.before, edit), row.after) << "text: " << row.name;
        // An Edit that does not apply carries no cursor: the caller leaves
        // the one it had, so that is what the row must state.
        EXPECT_EQ(edit.valid ? edit.anchor : row.anchor, row.newAnchor)
            << "anchor: " << row.name;
        EXPECT_EQ(edit.valid ? edit.position : row.position, row.newPosition)
            << "position: " << row.name;
    }
}


struct Where
{
    QString text;
    int     anchor;
    int     position;
};
} // namespace

// ═══════════════════ the pure commands, as tables ═════════════════════════

TEST(EditorCommandsSmartHome, LandsOnTheTextThenOnColumnZero)
{
    runTable(
        {
            {"from after the text", "    abc", 7, 7, "    abc", 4, 4},
            {"from the text", "    abc", 4, 4, "    abc", 0, 0},
            {"from inside the indent", "    abc", 2, 2, "    abc", 0, 0},
            {"from column 0", "    abc", 0, 0, "    abc", 0, 0},
            {"no indent at all", "abc", 2, 2, "abc", 0, 0},
            {"empty document", "", 0, 0, "", 0, 0},
            {"all whitespace, at the end", "    ", 4, 4, "    ", 0, 0},
            {"second line keeps its own start", "ab\n  cd", 7, 7, "ab\n  cd", 5, 5},
        },
        [](const QString &t, int a, int p) { return smartHome(t, a, p, false); });
}

TEST(EditorCommandsSmartHome, ShiftKeepsTheAnchor)
{
    const Edit edit = smartHome("    abc", 7, 7, true);
    EXPECT_EQ(edit.anchor, 7);
    EXPECT_EQ(edit.position, 4);
    EXPECT_TRUE(edit.isCursorOnly());
}

TEST(EditorCommandsLineClip, TakesTheWholeLineWithItsNewline)
{
    const LineClip middle = lineClip("one\ntwo\nthree", 5, 5);
    EXPECT_EQ(middle.clipboard, QString("two\n"));
    EXPECT_EQ(middle.start, 4);
    EXPECT_EQ(middle.length, 4);

    // The last line has no newline in the document; the clipboard gets one
    // anyway, because a line copy is a line wherever it lands.
    const LineClip last = lineClip("one\ntwo", 5, 5);
    EXPECT_EQ(last.clipboard, QString("two\n"));
    EXPECT_EQ(last.length, 3);

    const LineClip empty = lineClip("", 0, 0);
    EXPECT_EQ(empty.clipboard, QString("\n"));
    EXPECT_EQ(empty.length, 0);
}

TEST(EditorCommandsLinesTouched, ASelectionEndingAtColumnZeroStopsShort)
{
    const QString text = "one\ntwo\nthree";
    EXPECT_EQ(linesTouched(text, 0, 0).last, 0);
    // 0 .. start of line 1: line 1 is not included.
    EXPECT_EQ(linesTouched(text, 0, 4).last, 0);
    // one character into line 1: it is.
    EXPECT_EQ(linesTouched(text, 0, 5).last, 1);
    // A selection entirely inside one line is that one line.
    EXPECT_EQ(linesTouched(text, 5, 6).first, 1);
    EXPECT_EQ(linesTouched(text, 5, 6).last, 1);
    // Backwards selections read the same.
    EXPECT_EQ(linesTouched(text, 6, 5).first, 1);
}

TEST(EditorCommandsDeleteLines, TakesTheLinesAndLandsOnTheSameColumnBelow)
{
    runTable(
        {
            {"middle line", "one\ntwo\nthree", 5, 5, "one\nthree", 5, 5},
            {"first line", "one\ntwo", 1, 1, "two", 1, 1},
            {"column clamped to a shorter line", "aaaa\nbb\ncc", 3, 3, "bb\ncc", 2, 2},
            {"last line goes to the end of the one above", "one\ntwo", 5, 5, "one", 3, 3},
            {"the only line", "solo", 2, 2, "", 0, 0},
            {"a multi-line selection", "a\nb\nc\nd", 0, 3, "c\nd", 1, 1},
            {"a selection stopping at column 0", "a\nb\nc", 0, 2, "b\nc", 0, 0},
        },
        [](const QString &t, int a, int p) { return deleteLines(t, a, p); });
}

TEST(EditorCommandsPasteLineAbove, PutsTheLineAboveAndLeavesTheCaretOnItsOwn)
{
    const Edit edit = pasteLineAbove("one\ntwo", 5, 5, "copied\n");
    EXPECT_EQ(applyTo("one\ntwo", edit), QString("one\ncopied\ntwo"));
    // The caret was on 't' of "two"; it still is.
    EXPECT_EQ(edit.position, 12);

    // A clipboard without a trailing newline still pastes as a line.
    const Edit bare = pasteLineAbove("one", 0, 0, "x");
    EXPECT_EQ(applyTo("one", bare), QString("x\none"));
}

TEST(EditorCommandsMoveLines, CarriesTheSelectionAndStopsAtTheEnds)
{
    runTable(
        {
            {"down from the first line", "a\nb\nc", 0, 0, "b\na\nc", 2, 2},
            {"at the last line, nothing", "a\nb", 2, 2, "a\nb", 2, 2},
            {"a block of two", "a\nb\nc\nd", 0, 3, "c\na\nb\nd", 2, 5},
        },
        [](const QString &t, int a, int p) { return moveLines(t, a, p, 1); });

    runTable(
        {
            {"up from the second line", "a\nb\nc", 2, 2, "b\na\nc", 0, 0},
            {"at the first line, nothing", "a\nb", 0, 0, "a\nb", 0, 0},
            {"last line with no trailing newline", "a\nbb", 3, 3, "bb\na", 1, 1},
        },
        [](const QString &t, int a, int p) { return moveLines(t, a, p, -1); });

    EXPECT_FALSE(moveLines("a\nb", 0, 0, -1).valid) << "the first line cannot go up";
    EXPECT_FALSE(moveLines("a\nb", 2, 2, 1).valid) << "the last line cannot go down";
    EXPECT_FALSE(moveLines("a\nb", 0, 0, 2).valid) << "only one line at a time";
}

TEST(EditorCommandsDuplicateLines, SameTextEitherWayAndTheSelectionTellsThemApart)
{
    const QString text = "a\nb";
    const Edit    down = duplicateLines(text, 0, 0, false);
    const Edit    up   = duplicateLines(text, 0, 0, true);
    EXPECT_EQ(applyTo(text, down), QString("a\na\nb"));
    EXPECT_EQ(applyTo(text, up), QString("a\na\nb"));
    // Down moves onto the new copy, up stays on the same line numbers.
    EXPECT_EQ(down.position, 2);
    EXPECT_EQ(up.position, 0);

    // A block of two, and the last line without a trailing newline.
    const Edit block = duplicateLines("a\nbb\nc", 0, 4, false);
    EXPECT_EQ(applyTo("a\nbb\nc", block), QString("a\nbb\na\nbb\nc"));
    const Edit last = duplicateLines("a\nbb", 2, 2, false);
    EXPECT_EQ(applyTo("a\nbb", last), QString("a\nbb\nbb"));
}

TEST(EditorCommandsInsertLine, CopiesTheIndentAndPutsTheCaretAtItsEnd)
{
    runTable(
        {
            {"below", "  ab", 4, 4, "  ab\n  ", 7, 7},
            {"below, from mid-line", "  ab", 3, 3, "  ab\n  ", 7, 7},
            {"below, no indent", "ab", 1, 1, "ab\n", 3, 3},
            {"below, last line of several", "x\n\ty", 4, 4, "x\n\ty\n\t", 6, 6},
        },
        [](const QString &t, int a, int p) { return insertLine(t, a, p, false); });

    runTable(
        {
            {"above", "  ab", 4, 4, "  \n  ab", 2, 2},
            {"above, first line", "ab", 1, 1, "\nab", 0, 0},
            {"above, second line keeps its own indent", "a\n\tb", 4, 4, "a\n\t\n\tb", 3, 3},
        },
        [](const QString &t, int a, int p) { return insertLine(t, a, p, true); });
}

TEST(EditorCommandsSelectLine, TakesALineAndThenOneMore)
{
    const QString text = "aa\nbb\ncc";
    const Edit    once = selectLine(text, 1, 1);
    EXPECT_EQ(once.anchor, 0);
    EXPECT_EQ(once.position, 3);
    EXPECT_TRUE(once.isCursorOnly());

    const Edit twice = selectLine(text, once.anchor, once.position);
    EXPECT_EQ(twice.anchor, 0);
    EXPECT_EQ(twice.position, 6);

    const Edit thrice = selectLine(text, twice.anchor, twice.position);
    EXPECT_EQ(thrice.anchor, 0);
    EXPECT_EQ(thrice.position, 8) << "the last line has no newline to take";

    // From the last line it stops at the end of the document.
    EXPECT_EQ(selectLine(text, 7, 7).position, 8);
    // An empty document selects nothing and does not run off the end.
    EXPECT_EQ(selectLine("", 0, 0).position, 0);
}

TEST(EditorCommandsNewline, CopiesTheIndentAndNeverDeepensIt)
{
    runTable(
        {
            {"after text", "  ab", 4, 4, "  ab\n  ", 7, 7},
            {"no indent", "ab", 2, 2, "ab\n", 3, 3},
            {"after a label, no extra indent", "  lbl:", 6, 6, "  lbl:\n  ", 9, 9},
            {"inside the indent copies only what is left of the caret", "    ab", 2, 2,
             "  \n    ab", 5, 5},
            {"a selection is replaced", "  abcd", 3, 5, "  a\n  d", 6, 6},
            {"empty document", "", 0, 0, "\n", 1, 1},
            {"tab indent", "\t\tx", 3, 3, "\t\tx\n\t\t", 6, 6},
        },
        [](const QString &t, int a, int p) { return newlineWithIndent(t, a, p); });
}

TEST(EditorCommandsIndent, AddsOneUnitToEveryTouchedLine)
{
    runTable(
        {
            {"one line", "ab", 1, 1, "\tab", 2, 2},
            {"two lines", "a\nb", 0, 3, "\ta\n\tb", 0, 5},
            {"a selection stopping at column 0 leaves that line alone", "a\nb", 0, 2,
             "\ta\nb", 0, 3},
            {"blank lines are indented too", "a\n\nb", 0, 4, "\ta\n\t\n\tb", 0, 7},
        },
        [](const QString &t, int a, int p) { return indentLines(t, a, p); });
}

TEST(EditorCommandsOutdent, RemovesATabOrUpToFourSpaces)
{
    runTable(
        {
            {"a tab", "\tab", 3, 3, "ab", 2, 2},
            {"four spaces", "    ab", 6, 6, "ab", 2, 2},
            {"two spaces, all there is", "  ab", 4, 4, "ab", 2, 2},
            {"five spaces leaves one", "     ab", 7, 7, " ab", 3, 3},
            {"nothing to remove", "ab", 1, 1, "ab", 1, 1},
            {"mixed lines, each on its own terms", "\ta\n  b", 0, 5, "a\nb", 0, 2},
            {"a caret inside the removed run collapses to the start", "    ab", 2, 2,
             "ab", 0, 0},
        },
        [](const QString &t, int a, int p) { return outdentLines(t, a, p); });

    EXPECT_FALSE(outdentLines("ab", 0, 0).valid) << "nothing to outdent is not an edit";
}

TEST(EditorCommandsToggleComment, CommentsAtTheLeastIndentAndUncommentsExactly)
{
    runTable(
        {
            {"one line", "ab", 1, 1, "# ab", 3, 3},
            {"keeps the block's shape, marker at one column", "  a\n    b", 0, 9,
             "  # a\n  #   b", 0, 13},
            {"blank lines are skipped and do not decide", "a\n\nb", 0, 4, "# a\n\n# b", 0, 8},
            {"an indented line and a flush one use the flush column", "x\n    y", 0, 7,
             "# x\n#     y", 0, 11},
        },
        [](const QString &t, int a, int p) { return toggleComment(t, a, p); });

    runTable(
        {
            {"off again, with the space", "# ab", 3, 3, "ab", 1, 1},
            {"off again, without a space", "#ab", 2, 2, "ab", 1, 1},
            {"a partly commented block gets commented, not uncommented", "# a\nb", 0, 5,
             "# # a\n# b", 0, 9},
        },
        [](const QString &t, int a, int p) { return toggleComment(t, a, p); });

    EXPECT_FALSE(toggleComment("\n\n", 0, 2).valid) << "nothing but blank lines is not an edit";

    // A round trip gives the line back character for character.
    const QString start = "    addi t0, x0, 5";
    const Edit    on    = toggleComment(start, 0, 0);
    const QString commented = applyTo(start, on);
    EXPECT_EQ(commented, QString("    # addi t0, x0, 5"));
    EXPECT_EQ(applyTo(commented, toggleComment(commented, 0, 0)), start);
}
