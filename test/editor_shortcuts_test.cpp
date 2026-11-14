/**
 * @file editor_shortcuts_test.cpp
 * @brief The VS Code-style editing keys in the Editor tab's Input pane.
 *
 * Two layers, deliberately.
 *
 * The transformations live in `EditorCommands`, which knows nothing about a
 * widget, so most of what follows is a table of strings: document in,
 * document out, caret in, caret out. That is where the edge cases live -
 * the first line, the last line, no trailing newline, an empty document, a
 * selection that stops at column 0, blank lines inside a comment toggle.
 *
 * The wiring is then checked against a real offscreen `Editor` driven with
 * QKeyEvent, because a pure function that is never reached is worth nothing.
 * Those tests also cover what only the widget can answer: that one Ctrl+Z
 * undoes one command, that a read-only pane refuses every text-changing key,
 * and that none of this added an application shortcut.
 *
 * Qt6::Test is not linked, so keys are sent with QApplication::sendEvent and
 * signals are counted with lambdas rather than QSignalSpy.
 */
#include "ui/editor_tab/editor_commands.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"

#include <gtest/gtest.h>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <QPlainTextEdit>

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

void sendKey(QWidget *target, Qt::Key key, Qt::KeyboardModifiers mods = Qt::NoModifier,
             const QString &text = QString())
{
    QKeyEvent press(QEvent::KeyPress, key, mods, text);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, mods, text);
    QApplication::sendEvent(target, &release);
    QApplication::processEvents();
}

/// Puts `text` and the caret where a case wants them.
void place(QPlainTextEdit *editor, const QString &text, int anchor, int position)
{
    editor->setPlainText(text);
    QTextCursor cursor = editor->textCursor();
    cursor.setPosition(anchor);
    cursor.setPosition(position, QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    QApplication::processEvents();
    // A completion popup left over from a previous key takes Enter, Tab and
    // Backtab before the editor sees them - QCompleter filters them on the
    // widget itself, ahead of keyPressEvent. That is the completer's
    // contract and not what these cases are about, so the popup is closed
    // first. Closing it directly, rather than by sending Escape, because
    // Escape is one of the keys it intercepts.
    for (QWidget *top : QApplication::topLevelWidgets())
        if (top->isVisible() && qobject_cast<QAbstractItemView *>(top))
            top->hide();
    QApplication::processEvents();
}

struct Where
{
    QString text;
    int     anchor;
    int     position;
};

Where read(QPlainTextEdit *editor)
{
    const QTextCursor cursor = editor->textCursor();
    return {editor->toPlainText(), cursor.anchor(), cursor.position()};
}
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

// ═══════════════════ the wiring: a real editor, real keys ═════════════════

namespace
{
/// A writable offscreen Editor, shown so key events reach it the way they do
/// in the application.
struct Bench
{
    Editor editor;
    Bench()
    {
        editor.resize(600, 400);
        editor.show();
        QApplication::processEvents();
    }
};

/// Sends one key to a freshly placed document and reports the result.
Where after(Editor *editor, const QString &text, int anchor, int position, Qt::Key key,
           Qt::KeyboardModifiers mods = Qt::NoModifier, const QString &typed = QString())
{
    place(editor, text, anchor, position);
    sendKey(editor, key, mods, typed);
    return read(editor);
}
} // namespace

TEST(EditorKeysWiring, EveryImplementedKeyReachesItsCommand)
{
    Bench bench;
    Editor *editor = &bench.editor;

    struct Row
    {
        const char           *name;
        QString               before;
        int                   anchor;
        int                   position;
        Qt::Key               key;
        Qt::KeyboardModifiers mods;
        QString               after;
    };
    const QList<Row> rows = {
        {"smart home", "    ab", 6, 6, Qt::Key_Home, Qt::NoModifier, "    ab"},
        {"move line up", "a\nb", 2, 2, Qt::Key_Up, Qt::AltModifier, "b\na"},
        {"move line down", "a\nb", 0, 0, Qt::Key_Down, Qt::AltModifier, "b\na"},
        {"copy line down", "a", 0, 0, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier, "a\na"},
        {"copy line up", "a", 0, 0, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier, "a\na"},
        {"delete line", "a\nb", 0, 0, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, "b"},
        {"insert line below", "  a", 3, 3, Qt::Key_Return, Qt::ControlModifier, "  a\n  "},
        {"insert line above", "  a", 3, 3, Qt::Key_Return,
         Qt::ControlModifier | Qt::ShiftModifier, "  \n  a"},
        {"select line", "a\nb", 0, 0, Qt::Key_L, Qt::ControlModifier, "a\nb"},
        {"enter auto-indents", "  a", 3, 3, Qt::Key_Return, Qt::NoModifier, "  a\n  "},
        {"indent", "a", 0, 0, Qt::Key_BracketRight, Qt::ControlModifier, "\ta"},
        {"outdent", "\ta", 2, 2, Qt::Key_BracketLeft, Qt::ControlModifier, "a"},
        {"backtab outdents", "\ta", 2, 2, Qt::Key_Backtab, Qt::ShiftModifier, "a"},
        {"toggle comment on", "a", 0, 0, Qt::Key_Slash, Qt::ControlModifier, "# a"},
        {"toggle comment off", "# a", 0, 0, Qt::Key_Slash, Qt::ControlModifier, "a"},
        {"tab across lines indents", "a\nb", 0, 3, Qt::Key_Tab, Qt::NoModifier, "\ta\n\tb"},
    };
    for (const Row &row : rows)
    {
        const Where got = after(editor, row.before, row.anchor, row.position, row.key, row.mods);
        EXPECT_EQ(got.text, row.after) << row.name;
    }

    // Select-line changes no text but must change the selection.
    place(editor, "a\nb", 0, 0);
    sendKey(editor, Qt::Key_L, Qt::ControlModifier);
    EXPECT_EQ(read(editor).anchor, 0);
    EXPECT_EQ(read(editor).position, 2);
}

TEST(EditorKeysWiring, TabWithNoSelectionStillInsertsATab)
{
    Bench bench;
    // The one piece of existing behaviour this work is not allowed to move.
    const Where single = after(&bench.editor, "ab", 1, 1, Qt::Key_Tab, Qt::NoModifier, "\t");
    EXPECT_EQ(single.text, QString("a\tb"));

    // A selection inside ONE line is still the old behaviour: it replaces the
    // selection with a tab rather than indenting the line.
    const Where inside = after(&bench.editor, "abcd", 1, 3, Qt::Key_Tab, Qt::NoModifier, "\t");
    EXPECT_EQ(inside.text, QString("a\td"));
}

TEST(EditorKeysUndo, OneCtrlZUndoesOneCommand)
{
    Bench bench;
    Editor *editor = &bench.editor;

    struct Row
    {
        const char           *name;
        QString               before;
        int                   anchor;
        int                   position;
        Qt::Key               key;
        Qt::KeyboardModifiers mods;
    };
    const QList<Row> rows = {
        {"move lines", "a\nb\nc", 0, 0, Qt::Key_Down, Qt::AltModifier},
        {"copy lines", "a\nb\nc", 0, 0, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier},
        {"delete line", "a\nb\nc", 2, 2, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier},
        {"insert line below", "  a\nb", 3, 3, Qt::Key_Return, Qt::ControlModifier},
        {"insert line above", "  a\nb", 3, 3, Qt::Key_Return,
         Qt::ControlModifier | Qt::ShiftModifier},
        {"enter", "  a\nb", 3, 3, Qt::Key_Return, Qt::NoModifier},
        {"indent", "a\nb", 0, 3, Qt::Key_BracketRight, Qt::ControlModifier},
        {"outdent", "\ta\n\tb", 0, 5, Qt::Key_BracketLeft, Qt::ControlModifier},
        {"toggle comment", "a\n\nb", 0, 4, Qt::Key_Slash, Qt::ControlModifier},
        {"toggle comment off", "# a\n# b", 0, 7, Qt::Key_Slash, Qt::ControlModifier},
    };
    for (const Row &row : rows)
    {
        place(editor, row.before, row.anchor, row.position);
        sendKey(editor, row.key, row.mods);
        ASSERT_NE(read(editor).text, row.before) << row.name << " did nothing to undo";
        sendKey(editor, Qt::Key_Z, Qt::ControlModifier);
        EXPECT_EQ(read(editor).text, row.before) << "one Ctrl+Z did not undo " << row.name;
    }
}

TEST(EditorKeysClipboard, CopyAndCutWithNoSelectionTakeTheWholeLine)
{
    Bench       bench;
    Editor     *editor    = &bench.editor;
    QClipboard *clipboard = QApplication::clipboard();

    place(editor, "one\ntwo\nthree", 5, 5);
    sendKey(editor, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(clipboard->text(), QString("two\n"));
    ASSERT_NE(clipboard->mimeData(), nullptr);
    EXPECT_TRUE(clipboard->mimeData()->hasFormat(lineCopyMimeType))
        << "the clipboard is not marked as a line copy";
    EXPECT_EQ(read(editor).text, QString("one\ntwo\nthree")) << "copy changed the document";

    // The last line has no newline of its own; the clipboard still gets one.
    place(editor, "one\ntwo", 5, 5);
    sendKey(editor, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(clipboard->text(), QString("two\n"));

    // Cut takes the line out.
    place(editor, "one\ntwo\nthree", 5, 5);
    sendKey(editor, Qt::Key_X, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("one\nthree"));
    EXPECT_EQ(clipboard->text(), QString("two\n"));

    // With a selection, both are the ordinary commands again.
    place(editor, "abcd", 1, 3);
    sendKey(editor, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(clipboard->text(), QString("bc"));
    EXPECT_FALSE(clipboard->mimeData()->hasFormat(lineCopyMimeType))
        << "a fragment was marked as a line copy";
}

TEST(EditorKeysClipboard, PasteOfALineCopyGoesAboveAndLeavesTheCaretAlone)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    place(editor, "one\ntwo\nthree", 5, 5);
    sendKey(editor, Qt::Key_C, Qt::ControlModifier); // line copy of "two"
    place(editor, "one\ntwo\nthree", 10, 10);        // caret on "three"
    sendKey(editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("one\ntwo\ntwo\nthree"));
    // Still on "three", which is now one line further down.
    EXPECT_EQ(read(editor).position, 14);

    // An ordinary clipboard pastes the ordinary way, in the middle of a line.
    auto *plain = new QMimeData;
    plain->setText("XY");
    QApplication::clipboard()->setMimeData(plain);
    place(editor, "abc", 1, 1);
    sendKey(editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("aXYbc"));
}

TEST(EditorKeysReadOnly, AReadOnlyPaneRefusesEveryTextChangingKey)
{
    Bench   bench;
    Editor *editor = &bench.editor;
    editor->setReadOnly(true);

    const QString original = "  one\ntwo\nthree";
    struct Row { Qt::Key key; Qt::KeyboardModifiers mods; const char *name; };
    const QList<Row> rows = {
        {Qt::Key_Up, Qt::AltModifier, "move up"},
        {Qt::Key_Down, Qt::AltModifier, "move down"},
        {Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier, "copy up"},
        {Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier, "copy down"},
        {Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, "delete line"},
        {Qt::Key_Return, Qt::ControlModifier, "insert below"},
        {Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier, "insert above"},
        {Qt::Key_Return, Qt::NoModifier, "enter"},
        {Qt::Key_Slash, Qt::ControlModifier, "toggle comment"},
        {Qt::Key_BracketRight, Qt::ControlModifier, "indent"},
        {Qt::Key_BracketLeft, Qt::ControlModifier, "outdent"},
        {Qt::Key_Backtab, Qt::ShiftModifier, "backtab"},
        {Qt::Key_Tab, Qt::NoModifier, "tab"},
        {Qt::Key_X, Qt::ControlModifier, "cut line"},
        {Qt::Key_V, Qt::ControlModifier, "paste line"},
    };
    for (const Row &row : rows)
    {
        place(editor, original, 0, 9);
        sendKey(editor, row.key, row.mods);
        EXPECT_EQ(read(editor).text, original) << "read-only pane changed by " << row.name;
    }

    // The two that only read or move still work.
    place(editor, original, 5, 5);
    sendKey(editor, Qt::Key_Home);
    EXPECT_EQ(read(editor).position, 2) << "smart home should work in a read-only pane";
    place(editor, original, 0, 0);
    sendKey(editor, Qt::Key_L, Qt::ControlModifier);
    EXPECT_EQ(read(editor).position, 6) << "select line should work in a read-only pane";
}

TEST(EditorKeysUnicode, LineOperationsPreserveMultiByteCharacters)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    // One 2-byte character (lambda) and one 4-byte one (an emoji, which is a
    // surrogate PAIR in UTF-16 and so two QChars where it is one codepoint).
    const QString lambda = QString::fromUtf8("  \xce\xbb x");
    const QString emoji  = QString::fromUtf8("\xf0\x9f\x98\x80 y");
    const QString text   = lambda + "\n" + emoji + "\nplain";

    struct Row
    {
        const char           *name;
        Qt::Key               key;
        Qt::KeyboardModifiers mods;
        QString               expected;
    };
    const QList<Row> rows = {
        {"move down", Qt::Key_Down, Qt::AltModifier, emoji + "\n" + lambda + "\nplain"},
        {"copy up", Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier,
         lambda + "\n" + lambda + "\n" + emoji + "\nplain"},
        {"indent", Qt::Key_BracketRight, Qt::ControlModifier,
         "\t" + lambda + "\n" + emoji + "\nplain"},
        {"toggle comment", Qt::Key_Slash, Qt::ControlModifier,
         "  # " + QString::fromUtf8("\xce\xbb x") + "\n" + emoji + "\nplain"},
        {"delete line", Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier,
         emoji + "\nplain"},
    };
    for (const Row &row : rows)
    {
        place(editor, text, 0, 0);
        sendKey(editor, row.key, row.mods);
        EXPECT_EQ(read(editor).text.toUtf8(), row.expected.toUtf8()) << row.name;
    }

    // The emoji line specifically: moving it must not split the pair.
    place(editor, text, QString(lambda + "\n").size(), QString(lambda + "\n").size());
    sendKey(editor, Qt::Key_Up, Qt::AltModifier);
    EXPECT_EQ(read(editor).text.toUtf8(), QString(emoji + "\n" + lambda + "\nplain").toUtf8());

    // And an exact round trip through the comment toggle.
    place(editor, emoji, 0, 0);
    sendKey(editor, Qt::Key_Slash, Qt::ControlModifier);
    sendKey(editor, Qt::Key_Slash, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text.toUtf8(), emoji.toUtf8());
}

TEST(EditorKeysWiring, AnOpenCompletionPopupDoesNotSwallowTheModifiedKeys)
{
    // The completer filters Enter, Tab, Backtab and Escape on the widget,
    // ahead of keyPressEvent, and keeping those for the popup is right. The
    // MODIFIED combinations mean something else entirely and the popup has
    // no use for them, so they must still reach their command while a
    // completion is showing.
    Bench   bench;
    Editor *editor = &bench.editor;

    place(editor, "", 0, 0);
    // Typing is what opens the popup; whether it opens depends on what the
    // language service knows, so the case asserts on that rather than on a
    // popup it only hopes for.
    for (QChar c : QString("ad"))
        sendKey(editor, Qt::Key(c.unicode()), Qt::NoModifier, QString(c));

    bool popupShown = false;
    for (QWidget *top : QApplication::topLevelWidgets())
        if (top->isVisible() && qobject_cast<QAbstractItemView *>(top))
            popupShown = true;

    // kites_test links kites_core but not the compiled .qrc objects, and the
    // completion list is built from the instruction set that lives in one of
    // them - so no popup can open here and this case would pass without
    // exercising anything. It skips rather than lying, and the real check
    // runs in the out-of-tree editorrecon harness, which links the
    // resources.
    if (!popupShown)
        GTEST_SKIP() << "no completion popup in this build (no instruction resources)";

    const QString before = read(editor).text;
    sendKey(editor, Qt::Key_Return, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, before + "\n")
        << "Ctrl+Enter was swallowed (completion popup "
        << (popupShown ? "was" : "was not") << " open)";
}

// ═══════════════════ the Output pane, and the shortcut set ════════════════

namespace
{
QPlainTextEdit *paneNamed(MainWindow &window, const char *objectName)
{
    for (QPlainTextEdit *pane : window.findChildren<QPlainTextEdit *>())
        if (pane->objectName() == QLatin1String(objectName))
            return pane;
    return nullptr;
}
} // namespace

TEST(EditorOutputPane, NoTextChangingKeyReachesIt)
{
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QApplication::processEvents();

    QPlainTextEdit *output = paneNamed(window, "disassemblyTextEdit");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(output->isReadOnly()) << "the Output pane is supposed to be read-only";

    const QString original = "  addi t0, x0, 5\n  add t1, t0, t0\n  sub t2, t1, t0";
    struct Row { Qt::Key key; Qt::KeyboardModifiers mods; const char *name; };
    const QList<Row> rows = {
        {Qt::Key_Up, Qt::AltModifier, "move up"},
        {Qt::Key_Down, Qt::AltModifier, "move down"},
        {Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier, "copy up"},
        {Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier, "copy down"},
        {Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, "delete line"},
        {Qt::Key_Return, Qt::NoModifier, "enter"},
        {Qt::Key_Return, Qt::ControlModifier, "insert below"},
        {Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier, "insert above"},
        {Qt::Key_Slash, Qt::ControlModifier, "toggle comment"},
        {Qt::Key_BracketRight, Qt::ControlModifier, "indent"},
        {Qt::Key_BracketLeft, Qt::ControlModifier, "outdent"},
        {Qt::Key_Backtab, Qt::ShiftModifier, "backtab"},
        {Qt::Key_Tab, Qt::NoModifier, "tab"},
        {Qt::Key_X, Qt::ControlModifier, "cut line"},
        {Qt::Key_V, Qt::ControlModifier, "paste"},
    };
    for (const Row &row : rows)
    {
        place(output, original, 0, 0);
        sendKey(output, row.key, row.mods);
        EXPECT_EQ(read(output).text, original) << "the Output pane was changed by " << row.name;
    }

    // Ctrl+A and Ctrl+C still have to work there.
    place(output, original, 0, 0);
    sendKey(output, Qt::Key_A, Qt::ControlModifier);
    EXPECT_EQ(output->textCursor().selectedText().size(), original.size())
        << "Ctrl+A no longer selects all in the Output pane";
    sendKey(output, Qt::Key_C, Qt::ControlModifier);
    EXPECT_FALSE(QApplication::clipboard()->text().isEmpty())
        << "Ctrl+C no longer copies from the Output pane";
    EXPECT_EQ(read(output).text, original);
}

TEST(EditorShortcutSet, NothingNewBecameAnApplicationShortcut)
{
    MainWindow window;

    // The editing keys are handled in Editor::keyPressEvent on purpose. A
    // QAction would make this eight; a QShortcut would be invisible to the
    // count below while still being able to swallow a key before the editor
    // saw it. Both are asserted against.
    QStringList sequences;
    for (QAction *action : window.findChildren<QAction *>())
        if (!action->shortcut().isEmpty())
            sequences << action->shortcut().toString();
    EXPECT_EQ(sequences.size(), 7) << "the application-level shortcut count moved";
    QStringList distinct = sequences;
    distinct.removeDuplicates();
    EXPECT_EQ(distinct.size(), sequences.size()) << "two actions share a key sequence";

    int shortcutObjects = 0;
    for (QObject *child : window.findChildren<QObject *>())
        if (QLatin1String(child->metaObject()->className()) == QLatin1String("QShortcut"))
            ++shortcutObjects;
    EXPECT_EQ(shortcutObjects, 0) << "a QShortcut was added";

    // And none of the editing keys is owned by an action, which is what lets
    // the editor see them without any ShortcutOverride handling.
    const QList<QKeySequence> mine = {
        QKeySequence(Qt::CTRL | Qt::Key_Slash),        QKeySequence(Qt::CTRL | Qt::Key_L),
        QKeySequence(Qt::CTRL | Qt::Key_BracketRight), QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_K), QKeySequence(Qt::ALT | Qt::Key_Up),
        QKeySequence(Qt::ALT | Qt::Key_Down),          QKeySequence(Qt::CTRL | Qt::Key_Return),
        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return)};
    for (const QKeySequence &sequence : mine)
        for (QAction *action : window.findChildren<QAction *>())
            EXPECT_NE(action->shortcut(), sequence)
                << "'" << action->text().toStdString() << "' took "
                << sequence.toString().toStdString();
}

// ═══════════════════ the keys Qt already gets right ═══════════════════════
//
// Pinned, not re-implemented. If Qt's behaviour ever moves, these say so
// before a user does.

TEST(EditorNativeKeys, UndoRedoAndTheRedoAlternative)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    place(editor, "ab", 2, 2);
    for (QChar c : QString("XY"))
        sendKey(editor, Qt::Key(c.unicode()), Qt::NoModifier, QString(c));
    EXPECT_EQ(read(editor).text, QString("abXY"));

    sendKey(editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("ab")) << "Ctrl+Z";
    sendKey(editor, Qt::Key_Y, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("abXY")) << "Ctrl+Y";
    sendKey(editor, Qt::Key_Z, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("ab"));
    sendKey(editor, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
    EXPECT_EQ(read(editor).text, QString("abXY")) << "Ctrl+Shift+Z";
}

TEST(EditorNativeKeys, SelectionCopyCutAndPaste)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    place(editor, "abcd", 0, 0);
    sendKey(editor, Qt::Key_A, Qt::ControlModifier);
    EXPECT_EQ(editor->textCursor().selectedText(), QString("abcd")) << "Ctrl+A";

    place(editor, "abcd", 1, 3);
    sendKey(editor, Qt::Key_C, Qt::ControlModifier);
    EXPECT_EQ(QApplication::clipboard()->text(), QString("bc")) << "Ctrl+C";

    place(editor, "abcd", 1, 3);
    sendKey(editor, Qt::Key_X, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("ad")) << "Ctrl+X";

    // Set explicitly, so the case does not depend on what the cut above
    // happened to leave behind.
    auto *mime = new QMimeData;
    mime->setText("XY");
    QApplication::clipboard()->setMimeData(mime);
    place(editor, "abcd", 1, 1);
    sendKey(editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(read(editor).text, QString("aXYbcd")) << "Ctrl+V";
}

TEST(EditorNativeKeys, MovementAndWordDeletion)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    struct Row
    {
        const char           *name;
        QString               before;
        int                   anchor;
        int                   position;
        Qt::Key               key;
        Qt::KeyboardModifiers mods;
        QString               after;
        int                   newAnchor;
        int                   newPosition;
    };
    const QList<Row> rows = {
        {"End", "    abc", 0, 0, Qt::Key_End, Qt::NoModifier, "    abc", 7, 7},
        {"Ctrl+Home", "ab\ncd", 5, 5, Qt::Key_Home, Qt::ControlModifier, "ab\ncd", 0, 0},
        {"Ctrl+End", "ab\ncd", 0, 0, Qt::Key_End, Qt::ControlModifier, "ab\ncd", 5, 5},
        {"Ctrl+Left", "foo bar", 7, 7, Qt::Key_Left, Qt::ControlModifier, "foo bar", 4, 4},
        {"Ctrl+Right", "foo bar", 0, 0, Qt::Key_Right, Qt::ControlModifier, "foo bar", 4, 4},
        {"Ctrl+Shift+Left", "foo bar", 7, 7, Qt::Key_Left,
         Qt::ControlModifier | Qt::ShiftModifier, "foo bar", 7, 4},
        {"Ctrl+Shift+Right", "foo bar", 0, 0, Qt::Key_Right,
         Qt::ControlModifier | Qt::ShiftModifier, "foo bar", 0, 4},
        {"Shift+Right", "ab", 0, 0, Qt::Key_Right, Qt::ShiftModifier, "ab", 0, 1},
        {"Shift+Down", "ab\ncd", 0, 0, Qt::Key_Down, Qt::ShiftModifier, "ab\ncd", 0, 3},
        {"Ctrl+Backspace", "foo bar", 7, 7, Qt::Key_Backspace, Qt::ControlModifier, "foo ", 4, 4},
        {"Ctrl+Delete", "foo bar", 0, 0, Qt::Key_Delete, Qt::ControlModifier, "bar", 0, 0},
    };
    for (const Row &row : rows)
    {
        place(editor, row.before, row.anchor, row.position);
        sendKey(editor, row.key, row.mods);
        const Where got = read(editor);
        EXPECT_EQ(got.text, row.after) << row.name;
        EXPECT_EQ(got.anchor, row.newAnchor) << row.name << " anchor";
        EXPECT_EQ(got.position, row.newPosition) << row.name << " position";
    }
}

TEST(EditorNativeKeys, PageUpAndPageDownMoveTheCaret)
{
    Bench   bench;
    Editor *editor = &bench.editor;

    QStringList many;
    for (int i = 0; i < 200; ++i)
        many << QString("line %1").arg(i);
    const QString text = many.join('\n');

    place(editor, text, 0, 0);
    sendKey(editor, Qt::Key_PageDown);
    const int afterDown = read(editor).position;
    EXPECT_GT(afterDown, 0) << "PageDown did not move the caret";
    EXPECT_EQ(read(editor).text, text) << "PageDown changed the document";

    sendKey(editor, Qt::Key_PageUp);
    EXPECT_LT(read(editor).position, afterDown) << "PageUp did not move the caret back";
    EXPECT_EQ(read(editor).text, text) << "PageUp changed the document";
}
