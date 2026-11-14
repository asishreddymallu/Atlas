/**
 * @file editor_breakpoints_test.cpp
 * @brief Breakpoints follow their line through every edit. Issue 18.
 *
 * Two layers, the same split as the editing keys themselves.
 *
 * The line maps are pure: each command says where every line it rewrites
 * ends up, alongside what the text becomes, so "which line is the
 * breakpoint on now" is a table of ints beside the table of strings.
 *
 * The widget layer then checks the part no pure function can: that ordinary
 * Qt edits - typing, Enter, Backspace joining two lines, deleting a
 * selection, pasting - move breakpoints correctly through the anchors the
 * document maintains, and that the invariants hold after a long random
 * sequence of keys.
 */
#include "ui/editor_tab/editor_commands.h"

#include <gtest/gtest.h>

#include <QList>
#include <QString>
#include <algorithm>

using namespace Kites;
using namespace Kites::EditorCommands;

namespace
{
QString applyTo(const QString &text, const Edit &edit)
{
    if (!edit.valid)
        return text;
    QString out = text;
    out.replace(edit.start, edit.length, edit.text);
    return out;
}

/// The breakpoint lines (1-based, as the editor stores them) after `edit`,
/// sorted and with duplicates collapsed - which is what the editor does too.
QList<int> remap(const Edit &edit, const QList<int> &before)
{
    QList<int> after;
    for (int line : before)
    {
        const int mapped = edit.valid ? edit.lines.mapped(line - 1) : line - 1;
        if (mapped >= 0 && !after.contains(mapped + 1))
            after << mapped + 1;
    }
    std::sort(after.begin(), after.end());
    return after;
}

struct Case
{
    const char *name;
    QString     before;
    int         anchor;
    int         position;
    QString     after;
    QList<int>  breakpointsBefore;
    QList<int>  breakpointsAfter;
};

template <typename Command>
void runTable(const QList<Case> &cases, Command command)
{
    for (const Case &row : cases)
    {
        const Edit edit = command(row.before, row.anchor, row.position);
        EXPECT_EQ(applyTo(row.before, edit), row.after) << "text: " << row.name;
        EXPECT_EQ(remap(edit, row.breakpointsBefore), row.breakpointsAfter)
            << "breakpoints: " << row.name;
    }
}

const QString kFive = "l1\nl2\nl3\nl4\nl5";
} // namespace

TEST(BreakpointMapIdentity, ALineMapWithNoSpanMovesNothing)
{
    LineMap map; // the default: lastLine < firstLine
    for (int line = 0; line < 5; ++line)
        EXPECT_EQ(map.mapped(line), line);
}

TEST(BreakpointMapEnter, ColumnZeroCarriesTheLineDownAndElsewhereKeepsIt)
{
    runTable(
        {
            {"Enter at column 0 of line 3", kFive, 6, 6, "l1\nl2\n\nl3\nl4\nl5",
             {2, 3, 4}, {2, 4, 5}},
            {"Enter mid-line of line 3", kFive, 7, 7, "l1\nl2\nl\n3\nl4\nl5",
             {2, 3, 4}, {2, 3, 5}},
            {"Enter at end of line 3", kFive, 8, 8, "l1\nl2\nl3\n\nl4\nl5",
             {2, 3, 4}, {2, 3, 5}},
            {"Enter at column 0 of the first line", kFive, 0, 0, "\nl1\nl2\nl3\nl4\nl5",
             {1, 2}, {2, 3}},
            {"Enter at the end of the last line", kFive, 14, 14, kFive + "\n",
             {5}, {5}},
            {"Enter in an empty document", "", 0, 0, "\n", {1}, {2}},
            // [3,11) is all of lines 2, 3 and 4 but not line 4's newline, so
            // what is left of line 2 and what is left of line 4 are both
            // empty - and both keep their breakpoints.
            {"Enter over a selection spanning lines 2..4", kFive, 3, 11,
             "l1\n\n\nl5", {1, 2, 3, 4, 5}, {1, 2, 3, 4}},
        },
        [](const QString &t, int a, int p) { return newlineWithIndent(t, a, p); });
}

TEST(BreakpointMapMoveLines, TheBlockAndTheLineItJumpedOverSwap)
{
    runTable(
        {
            {"line 2 down", kFive, 3, 3, "l1\nl3\nl2\nl4\nl5", {2, 3, 4}, {2, 3, 4}},
            {"line 2 down, breakpoint only on 2", kFive, 3, 3, "l1\nl3\nl2\nl4\nl5",
             {2}, {3}},
            {"line 2 down, breakpoint only on 3", kFive, 3, 3, "l1\nl3\nl2\nl4\nl5",
             {3}, {2}},
            {"last line cannot go down", kFive, 12, 12, kFive, {5}, {5}},
            {"a block of two moves down", kFive, 3, 8, "l1\nl4\nl2\nl3\nl5",
             {2, 3, 4}, {2, 3, 4}},
            {"a block of two, breakpoint on the jumped line", kFive, 3, 8,
             "l1\nl4\nl2\nl3\nl5", {4}, {2}},
        },
        [](const QString &t, int a, int p) { return moveLines(t, a, p, 1); });

    runTable(
        {
            {"line 3 up", kFive, 6, 6, "l1\nl3\nl2\nl4\nl5", {2, 3}, {2, 3}},
            {"line 3 up, breakpoint only on 3", kFive, 6, 6, "l1\nl3\nl2\nl4\nl5",
             {3}, {2}},
            {"line 3 up, breakpoint only on 2", kFive, 6, 6, "l1\nl3\nl2\nl4\nl5",
             {2}, {3}},
            {"first line cannot go up", kFive, 0, 0, kFive, {1}, {1}},
        },
        [](const QString &t, int a, int p) { return moveLines(t, a, p, -1); });
}

TEST(BreakpointMapCopyLines, TheOriginalKeepsItAndTheCopyGetsNone)
{
    runTable(
        {
            {"copy line 2 down", kFive, 3, 3, "l1\nl2\nl2\nl3\nl4\nl5",
             {2, 3}, {2, 4}},
            {"copy line 2 down, nothing on 2", kFive, 3, 3, "l1\nl2\nl2\nl3\nl4\nl5",
             {4}, {5}},
            {"copy a block of two down", kFive, 3, 8, "l1\nl2\nl3\nl2\nl3\nl4\nl5",
             {2, 3, 4}, {2, 3, 6}},
        },
        [](const QString &t, int a, int p) { return duplicateLines(t, a, p, false); });

    runTable(
        {
            {"copy line 2 up", kFive, 3, 3, "l1\nl2\nl2\nl3\nl4\nl5",
             {2, 3}, {3, 4}},
            {"copy a block of two up", kFive, 3, 8, "l1\nl2\nl3\nl2\nl3\nl4\nl5",
             {2, 3, 4}, {4, 5, 6}},
        },
        [](const QString &t, int a, int p) { return duplicateLines(t, a, p, true); });
}

TEST(BreakpointMapDeleteLines, DeletedLinesLoseThemAndTheRestMoveUp)
{
    runTable(
        {
            {"delete line 3", kFive, 6, 6, "l1\nl2\nl4\nl5", {2, 3, 4}, {2, 3}},
            {"delete the first line", kFive, 0, 0, "l2\nl3\nl4\nl5", {1, 2}, {1}},
            {"delete the last line", kFive, 12, 12, "l1\nl2\nl3\nl4", {4, 5}, {4}},
            {"delete a block of two", kFive, 3, 8, "l1\nl4\nl5", {1, 2, 3, 4}, {1, 2}},
            {"a selection stopping at column 0 spares that line", kFive, 3, 6,
             "l1\nl3\nl4\nl5", {2, 3}, {2}},
            {"delete the only line", "solo", 0, 0, "", {1}, {}},
        },
        [](const QString &t, int a, int p) { return deleteLines(t, a, p); });
}

TEST(BreakpointMapInsertLine, AboveCarriesTheCurrentLineDownAndBelowDoesNot)
{
    runTable(
        {
            {"below line 3", kFive, 6, 6, "l1\nl2\nl3\n\nl4\nl5", {3, 4}, {3, 5}},
            {"below the last line", kFive, 14, 14, kFive + "\n", {5}, {5}},
        },
        [](const QString &t, int a, int p) { return insertLine(t, a, p, false); });

    runTable(
        {
            {"above line 3", kFive, 6, 6, "l1\nl2\n\nl3\nl4\nl5", {3, 4}, {4, 5}},
            {"above the first line", kFive, 0, 0, "\nl1\nl2\nl3\nl4\nl5", {1}, {2}},
        },
        [](const QString &t, int a, int p) { return insertLine(t, a, p, true); });
}

TEST(BreakpointMapPasteLine, PastingAboveShiftsTheLineAndItsBreakpointDown)
{
    const Edit one = pasteLineAbove(kFive, 6, 6, "copied\n");
    EXPECT_EQ(applyTo(kFive, one), QString("l1\nl2\ncopied\nl3\nl4\nl5"));
    EXPECT_EQ(remap(one, {2, 3, 4}), (QList<int>{2, 4, 5}));

    const Edit two = pasteLineAbove(kFive, 6, 6, "a\nb\n");
    EXPECT_EQ(remap(two, {3}), (QList<int>{5}));

    // Above the first line.
    const Edit top = pasteLineAbove(kFive, 0, 0, "x\n");
    EXPECT_EQ(remap(top, {1, 2}), (QList<int>{2, 3}));
}

TEST(BreakpointMapInPlaceCommands, IndentOutdentAndCommentMoveNothing)
{
    const QList<int> all = {1, 2, 3, 4, 5};
    for (auto command : {&indentLines, &outdentLines, &toggleComment})
    {
        const Edit edit = command("\ta\n\tb\n\tc\n\td\n\te", 0, 14);
        ASSERT_TRUE(edit.valid);
        EXPECT_EQ(remap(edit, all), all) << "an in-place command moved a breakpoint";
    }

    // And the lines below an in-place command do not move either.
    const Edit one = toggleComment(kFive, 0, 2);
    EXPECT_EQ(remap(one, all), all);
}

TEST(BreakpointMapEdges, NoTrailingNewlineAndEmptyDocuments)
{
    // The last line has no newline of its own in any of these.
    EXPECT_EQ(remap(deleteLines("a\nb", 2, 2), {1, 2}), (QList<int>{1}));
    EXPECT_EQ(remap(moveLines("a\nb", 2, 2, -1), {2}), (QList<int>{1}));
    EXPECT_EQ(remap(duplicateLines("a\nb", 2, 2, false), {2}), (QList<int>{2}));

    // An empty document is one empty line.
    EXPECT_EQ(remap(insertLine("", 0, 0, true), {1}), (QList<int>{2}));
    EXPECT_EQ(remap(insertLine("", 0, 0, false), {1}), (QList<int>{1}));

    // A command that does not apply moves nothing.
    EXPECT_EQ(remap(moveLines("a\nb", 0, 0, -1), {1, 2}), (QList<int>{1, 2}));
    EXPECT_EQ(remap(outdentLines("ab", 0, 0), {1}), (QList<int>{1}));

    // A cursor-only command moves nothing.
    EXPECT_EQ(remap(smartHome(kFive, 8, 8, false), {1, 2, 3, 4, 5}),
              (QList<int>{1, 2, 3, 4, 5}));
    EXPECT_EQ(remap(selectLine(kFive, 8, 8), {1, 2, 3, 4, 5}), (QList<int>{1, 2, 3, 4, 5}));
}

TEST(BreakpointMapInvariants, NoMapEverProducesADuplicateOrGrowsTheCount)
{
    // Every command, over every selection in a five-line document, with a
    // breakpoint on every line: the result must stay in range, stay unique,
    // and never be larger than it started.
    const QList<int> all = {1, 2, 3, 4, 5};
    int              checked = 0;

    for (int a = 0; a <= kFive.size(); ++a)
        for (int p = 0; p <= kFive.size(); ++p)
        {
            const QList<Edit> edits = {
                newlineWithIndent(kFive, a, p), moveLines(kFive, a, p, -1),
                moveLines(kFive, a, p, 1),      duplicateLines(kFive, a, p, true),
                duplicateLines(kFive, a, p, false), deleteLines(kFive, a, p),
                insertLine(kFive, a, p, true),  insertLine(kFive, a, p, false),
                indentLines(kFive, a, p),       outdentLines(kFive, a, p),
                toggleComment(kFive, a, p),     pasteLineAbove(kFive, a, p, "x\n")};
            for (const Edit &edit : edits)
            {
                const QList<int> after = remap(edit, all);
                const QString    text  = applyTo(kFive, edit);
                const int        lines = text.count(QLatin1Char('\n')) + 1;
                ++checked;
                EXPECT_LE(after.size(), all.size()) << "a map grew the count";
                for (int i = 0; i < after.size(); ++i)
                {
                    EXPECT_GE(after.at(i), 1) << "a breakpoint went above line 1";
                    EXPECT_LE(after.at(i), lines)
                        << "a breakpoint went past the end (" << lines << " lines)";
                    if (i)
                        EXPECT_NE(after.at(i), after.at(i - 1)) << "a duplicate survived";
                }
            }
        }
    EXPECT_GT(checked, 2000) << "the sweep did not actually run";
}

// ═══════════════════ the widget: real keys, real anchors ══════════════════

#include "ui/editor_tab/main_editor.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QKeyEvent>
#include <QMimeData>
#include <random>

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

/// A writable offscreen Editor, shown so keys reach it as they do in the
/// application.
struct Bench
{
    Editor editor;
    Bench()
    {
        editor.resize(600, 400);
        editor.show();
        QApplication::processEvents();
    }

    /// Text, caret and breakpoints, in that order - setPlainText keeps the
    /// numbers it already had, so the breakpoints go on afterwards.
    void place(const QString &text, int anchor, int position, const QList<int> &breakpoints)
    {
        editor.setPlainText(text);
        editor.setBreakpoints({});
        for (int line : breakpoints)
            editor.toggleBreakpointAtLine(line);
        QTextCursor cursor = editor.textCursor();
        cursor.setPosition(anchor);
        cursor.setPosition(position, QTextCursor::KeepAnchor);
        editor.setTextCursor(cursor);
        QApplication::processEvents();
        // A completion popup left from a previous key takes Enter and Tab
        // before the editor sees them; that is the completer's contract and
        // not what these cases are about.
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->isVisible() && qobject_cast<QAbstractItemView *>(top))
                top->hide();
        QApplication::processEvents();
    }

    QList<int> breakpoints() const
    {
        QList<int> out;
        for (uint64_t line : editor.getBreakpoints())
            out << int(line);
        return out;
    }
};

struct KeyCase
{
    const char           *name;
    QString               before;
    int                   anchor;
    int                   position;
    QList<int>            breakpointsBefore;
    Qt::Key               key;
    Qt::KeyboardModifiers mods;
    QString               after;
    QList<int>            breakpointsAfter;
};

void runKeys(Bench &bench, const QList<KeyCase> &cases)
{
    for (const KeyCase &row : cases)
    {
        bench.place(row.before, row.anchor, row.position, row.breakpointsBefore);
        ASSERT_EQ(bench.breakpoints(), row.breakpointsBefore)
            << "setup went wrong for: " << row.name;
        sendKey(&bench.editor, row.key, row.mods);
        EXPECT_EQ(bench.editor.toPlainText(), row.after) << "text: " << row.name;
        EXPECT_EQ(bench.breakpoints(), row.breakpointsAfter) << "breakpoints: " << row.name;
    }
}

const QString kFiveW = "l1\nl2\nl3\nl4\nl5";
} // namespace

TEST(BreakpointKeysOrdinaryEdits, EnterBackspaceDeleteAndPaste)
{
    Bench bench;

    // Plain Enter, which goes through the editor's own auto-indent command.
    runKeys(bench,
            {
                {"Enter at column 0 of line 3", kFiveW, 6, 6, {2, 3, 4}, Qt::Key_Return,
                 Qt::NoModifier, "l1\nl2\n\nl3\nl4\nl5", {2, 4, 5}},
                {"Enter mid-line of line 3", kFiveW, 7, 7, {2, 3, 4}, Qt::Key_Return,
                 Qt::NoModifier, "l1\nl2\nl\n3\nl4\nl5", {2, 3, 5}},
                {"Enter at the end of line 3", kFiveW, 8, 8, {2, 3, 4}, Qt::Key_Return,
                 Qt::NoModifier, "l1\nl2\nl3\n\nl4\nl5", {2, 3, 5}},
            });

    // Backspace joining two lines - pure Qt, no command involved.
    runKeys(bench,
            {
                {"Backspace at column 0 of line 3, both marked", kFiveW, 6, 6, {2, 3},
                 Qt::Key_Backspace, Qt::NoModifier, "l1\nl2l3\nl4\nl5", {2}},
                {"Backspace at column 0 of line 3, only the lower marked", kFiveW, 6, 6,
                 {3, 5}, Qt::Key_Backspace, Qt::NoModifier, "l1\nl2l3\nl4\nl5", {2, 4}},
                {"Backspace at column 0 of line 3, only the upper marked", kFiveW, 6, 6,
                 {2, 5}, Qt::Key_Backspace, Qt::NoModifier, "l1\nl2l3\nl4\nl5", {2, 4}},
            });

    // Deleting a multi-line selection.
    bench.place(kFiveW, 3, 11, {1, 2, 3, 4, 5});
    sendKey(&bench.editor, Qt::Key_Delete);
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\n\nl5"));
    // Lines 2, 3 and 4's text is all gone; what is left of line 2 is the
    // partly-kept first line and keeps one.
    EXPECT_EQ(bench.breakpoints(), (QList<int>{1, 2, 3}));

    // Typing inside a line changes nothing.
    bench.place(kFiveW, 7, 7, {2, 3, 4});
    sendKey(&bench.editor, Qt::Key_X, Qt::NoModifier, "X");
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nl2\nlX3\nl4\nl5"));
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 3, 4}));
}

TEST(BreakpointKeysOrdinaryEdits, PastingMultipleLinesShiftsWhatIsBelow)
{
    Bench bench;
    auto *mime = new QMimeData;
    mime->setText("a\nb\n");
    QApplication::clipboard()->setMimeData(mime);

    bench.place(kFiveW, 6, 6, {2, 3, 4}); // caret at column 0 of line 3
    sendKey(&bench.editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nl2\na\nb\nl3\nl4\nl5"));
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 5, 6}));

    auto *again = new QMimeData;
    again->setText("a\nb\n");
    QApplication::clipboard()->setMimeData(again);
    bench.place(kFiveW, 7, 7, {2, 3, 4}); // mid-line of line 3
    sendKey(&bench.editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nl2\nla\nb\n3\nl4\nl5"));
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 3, 6}));
}

TEST(BreakpointKeysLineCommands, EveryCommandCarriesTheBreakpointsItShould)
{
    Bench bench;
    runKeys(
        bench,
        {
            {"Alt+Down carries both lines", kFiveW, 3, 3, {2, 3}, Qt::Key_Down,
             Qt::AltModifier, "l1\nl3\nl2\nl4\nl5", {2, 3}},
            {"Alt+Down, only the moved line marked", kFiveW, 3, 3, {2}, Qt::Key_Down,
             Qt::AltModifier, "l1\nl3\nl2\nl4\nl5", {3}},
            {"Alt+Down, only the jumped line marked", kFiveW, 3, 3, {3}, Qt::Key_Down,
             Qt::AltModifier, "l1\nl3\nl2\nl4\nl5", {2}},
            {"Alt+Up, only the moved line marked", kFiveW, 6, 6, {3}, Qt::Key_Up,
             Qt::AltModifier, "l1\nl3\nl2\nl4\nl5", {2}},
            {"Alt+Up at the first line does nothing", kFiveW, 0, 0, {1, 2}, Qt::Key_Up,
             Qt::AltModifier, kFiveW, {1, 2}},
            {"Shift+Alt+Down: the original keeps it, the copy gets none", kFiveW, 3, 3,
             {2, 4}, Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier,
             "l1\nl2\nl2\nl3\nl4\nl5", {2, 5}},
            {"Shift+Alt+Up: the copy goes above, the original moves down", kFiveW, 3, 3,
             {2, 4}, Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier,
             "l1\nl2\nl2\nl3\nl4\nl5", {3, 5}},
            {"Ctrl+Shift+K removes it and pulls the rest up", kFiveW, 6, 6, {2, 3, 4},
             Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, "l1\nl2\nl4\nl5", {2, 3}},
            {"Ctrl+Enter inserts below, nothing moves down from the caret's line", kFiveW,
             6, 6, {3, 4}, Qt::Key_Return, Qt::ControlModifier, "l1\nl2\nl3\n\nl4\nl5",
             {3, 5}},
            {"Ctrl+Shift+Enter inserts above and carries the current line", kFiveW, 6, 6,
             {3, 4}, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier,
             "l1\nl2\n\nl3\nl4\nl5", {4, 5}},
            {"Ctrl+/ moves nothing", kFiveW, 6, 6, {1, 2, 3, 4, 5}, Qt::Key_Slash,
             Qt::ControlModifier, "l1\nl2\n# l3\nl4\nl5", {1, 2, 3, 4, 5}},
            {"Ctrl+] moves nothing", kFiveW, 6, 6, {1, 2, 3, 4, 5}, Qt::Key_BracketRight,
             Qt::ControlModifier, "l1\nl2\n\tl3\nl4\nl5", {1, 2, 3, 4, 5}},
            {"Ctrl+[ moves nothing", "l1\n\tl2\nl3", 4, 4, {1, 2, 3}, Qt::Key_BracketLeft,
             Qt::ControlModifier, "l1\nl2\nl3", {1, 2, 3}},
            {"Shift+Tab moves nothing", "l1\n\tl2\nl3", 4, 4, {1, 2, 3}, Qt::Key_Backtab,
             Qt::ShiftModifier, "l1\nl2\nl3", {1, 2, 3}},
            {"Tab across lines moves nothing", kFiveW, 3, 8, {1, 2, 3, 4, 5}, Qt::Key_Tab,
             Qt::NoModifier, "l1\n\tl2\n\tl3\nl4\nl5", {1, 2, 3, 4, 5}},
        });
}

TEST(BreakpointKeysLineClipboard, CutRemovesItAndAPasteAboveShiftsLinesDown)
{
    Bench bench;

    // Ctrl+X on a whole line takes its breakpoint with it.
    bench.place(kFiveW, 6, 6, {2, 3, 4});
    sendKey(&bench.editor, Qt::Key_X, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nl2\nl4\nl5"));
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 3}));

    // And the line copy now on the clipboard pastes ABOVE, shifting down.
    bench.place(kFiveW, 6, 6, {2, 3, 4});
    sendKey(&bench.editor, Qt::Key_V, Qt::ControlModifier);
    EXPECT_EQ(bench.editor.toPlainText(), QString("l1\nl2\nl3\nl3\nl4\nl5"));
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4, 5}));
}

TEST(BreakpointKeysUndo, UndoAndRedoLeaveBreakpointsInRange)
{
    Bench bench;
    struct Row { const char *name; Qt::Key key; Qt::KeyboardModifiers mods; };
    const QList<Row> rows = {
        {"Alt+Down", Qt::Key_Down, Qt::AltModifier},
        {"Alt+Up", Qt::Key_Up, Qt::AltModifier},
        {"Shift+Alt+Down", Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier},
        {"Shift+Alt+Up", Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier},
        {"Ctrl+Shift+K", Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier},
        {"Ctrl+Enter", Qt::Key_Return, Qt::ControlModifier},
        {"Ctrl+Shift+Enter", Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier},
        {"Enter", Qt::Key_Return, Qt::NoModifier},
        {"Ctrl+/", Qt::Key_Slash, Qt::ControlModifier},
        {"Ctrl+]", Qt::Key_BracketRight, Qt::ControlModifier},
    };
    for (const Row &row : rows)
    {
        bench.place(kFiveW, 6, 6, {1, 3, 5});
        sendKey(&bench.editor, row.key, row.mods);
        sendKey(&bench.editor, Qt::Key_Z, Qt::ControlModifier);
        EXPECT_EQ(bench.editor.toPlainText(), kFiveW) << "undo of " << row.name;
        for (int line : bench.breakpoints())
        {
            EXPECT_GE(line, 1) << "after undoing " << row.name;
            EXPECT_LE(line, bench.editor.document()->blockCount())
                << "after undoing " << row.name;
        }

        sendKey(&bench.editor, Qt::Key_Y, Qt::ControlModifier);
        for (int line : bench.breakpoints())
        {
            EXPECT_GE(line, 1) << "after redoing " << row.name;
            EXPECT_LE(line, bench.editor.document()->blockCount())
                << "after redoing " << row.name;
        }
    }
}

TEST(BreakpointKeysDocumentSwap, LoadingAFileLeavesTheNumbersWhereTheyWere)
{
    // Exactly what B0.4 measured on the shipped build before any of this:
    // setPlainText does not touch the breakpoint numbers, even when they
    // fall outside the new document.
    Bench bench;
    bench.place(kFiveW, 0, 0, {2, 4});
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4}));

    bench.editor.setPlainText("only\ntwo");
    QApplication::processEvents();
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4}))
        << "loading a shorter file changed the numbers";

    bench.editor.setPlainText("a\nb\nc\nd\ne\nf");
    QApplication::processEvents();
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 4}));

    // A user wiping the document is NOT a file load, and does remove them.
    bench.place(kFiveW, 0, 0, {2, 4});
    QTextCursor all = bench.editor.textCursor();
    all.select(QTextCursor::Document);
    bench.editor.setTextCursor(all);
    sendKey(&bench.editor, Qt::Key_Delete);
    EXPECT_EQ(bench.breakpoints(), (QList<int>{}))
        << "select-all-and-delete should not behave like loading a file";
}

TEST(BreakpointKeysInvariants, TwoHundredRandomKeysKeepThemLegal)
{
    Bench bench;
    // ADJACENT lines, deliberately. With gaps between them a join or a
    // delete can never land two breakpoints on one line, and the "no
    // duplicates" assertion below never gets to do anything - which a
    // negative control proved by disabling the de-duplication and watching
    // this case pass anyway.
    bench.place("one\ntwo\nthree\nfour\nfive\nsix\nseven\neight", 0, 0,
                {1, 2, 3, 4, 5});
    const int started = bench.breakpoints().size();

    struct Step { Qt::Key key; Qt::KeyboardModifiers mods; const char *text; };
    const QList<Step> steps = {
        {Qt::Key_Return, Qt::NoModifier, ""},
        {Qt::Key_Return, Qt::ControlModifier, ""},
        {Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier, ""},
        {Qt::Key_Down, Qt::AltModifier, ""},
        {Qt::Key_Up, Qt::AltModifier, ""},
        {Qt::Key_Down, Qt::AltModifier | Qt::ShiftModifier, ""},
        {Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier, ""},
        {Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, ""},
        {Qt::Key_Slash, Qt::ControlModifier, ""},
        {Qt::Key_BracketRight, Qt::ControlModifier, ""},
        {Qt::Key_BracketLeft, Qt::ControlModifier, ""},
        {Qt::Key_Backspace, Qt::NoModifier, ""},
        {Qt::Key_Delete, Qt::NoModifier, ""},
        {Qt::Key_Z, Qt::ControlModifier, ""},
        {Qt::Key_X, Qt::ControlModifier, ""},
        {Qt::Key_V, Qt::ControlModifier, ""},
        {Qt::Key_Down, Qt::NoModifier, ""},
        {Qt::Key_Up, Qt::NoModifier, ""},
        {Qt::Key_Right, Qt::NoModifier, ""},
        {Qt::Key_A, Qt::NoModifier, "a"},
    };

    std::mt19937 rng(20260115); // fixed, so a failure is reproducible
    std::uniform_int_distribution<int> pick(0, int(steps.size()) - 1);

    for (int i = 0; i < 200; ++i)
    {
        const Step &step = steps.at(pick(rng));
        sendKey(&bench.editor, step.key, step.mods, QString::fromLatin1(step.text));

        const QList<int> lines  = bench.breakpoints();
        const int        blocks = bench.editor.document()->blockCount();
        ASSERT_LE(lines.size(), started) << "the count grew at step " << i;
        for (int j = 0; j < lines.size(); ++j)
        {
            ASSERT_GE(lines.at(j), 1) << "below line 1 at step " << i;
            ASSERT_LE(lines.at(j), blocks)
                << "past the end (" << blocks << " lines) at step " << i;
            if (j)
                ASSERT_NE(lines.at(j), lines.at(j - 1)) << "a duplicate at step " << i;
        }
    }
}

TEST(BreakpointKeysReadOnly, AReadOnlyPaneNeitherGainsNorLosesThem)
{
    Bench bench;
    bench.place(kFiveW, 6, 6, {2, 3, 4});
    bench.editor.setReadOnly(true);

    struct Row { Qt::Key key; Qt::KeyboardModifiers mods; const char *name; };
    const QList<Row> rows = {
        {Qt::Key_Return, Qt::NoModifier, "Enter"},
        {Qt::Key_Down, Qt::AltModifier, "Alt+Down"},
        {Qt::Key_Up, Qt::AltModifier | Qt::ShiftModifier, "Shift+Alt+Up"},
        {Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier, "Ctrl+Shift+K"},
        {Qt::Key_Slash, Qt::ControlModifier, "Ctrl+/"},
        {Qt::Key_X, Qt::ControlModifier, "Ctrl+X"},
        {Qt::Key_V, Qt::ControlModifier, "Ctrl+V"},
        {Qt::Key_Backspace, Qt::NoModifier, "Backspace"},
    };
    for (const Row &row : rows)
    {
        sendKey(&bench.editor, row.key, row.mods);
        EXPECT_EQ(bench.editor.toPlainText(), kFiveW) << "text changed by " << row.name;
        EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 3, 4}))
            << "breakpoints changed by " << row.name;
    }
}

TEST(BreakpointKeysInvariants, ALineNumberPastTheEndIsDroppedWhenItArrives)
{
    // normaliseBreakpoints() clamps to the document, and this is the input
    // that reaches that clamp: toggleBreakpointAtLine() is public and
    // pushes whatever number it is handed before normalising. The gutter
    // only ever passes a line it drew, but nothing in the signature says so.
    Bench bench;
    bench.place("one\ntwo\nthree", 0, 0, {});

    bench.editor.toggleBreakpointAtLine(99);
    EXPECT_EQ(bench.breakpoints(), (QList<int>{}))
        << "a line past the end of the document was kept";

    bench.editor.toggleBreakpointAtLine(0);
    EXPECT_EQ(bench.breakpoints(), (QList<int>{})) << "line 0 was kept";

    bench.editor.toggleBreakpointAtLine(3);
    EXPECT_EQ(bench.breakpoints(), (QList<int>{3})) << "a line that exists was dropped";
}

TEST(BreakpointKeysDocumentSwap, AnOutOfRangeNumberGoesOnTheFirstEditAfterTheLoad)
{
    // The other way to reach the clamp. setBreakpoints() deliberately keeps
    // the numbers it is given, in range or not - that is what loading a
    // shorter file has always done. The first EDIT then recomputes from the
    // anchors, and a number with no line to anchor to goes.
    Bench bench;
    bench.place("one\ntwo\nthree\nfour", 0, 0, {});
    bench.editor.setBreakpoints({2, 9});
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 9}))
        << "setBreakpoints should keep what it is given";

    bench.editor.setPlainText("a\nb");
    QApplication::processEvents();
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2, 9}))
        << "loading a file should still leave the numbers alone";

    // Now a real edit.
    QTextCursor cursor = bench.editor.textCursor();
    cursor.setPosition(0);
    bench.editor.setTextCursor(cursor);
    sendKey(&bench.editor, Qt::Key_X, Qt::NoModifier, "X");
    EXPECT_EQ(bench.breakpoints(), (QList<int>{2}))
        << "the out-of-range number survived an edit";
}
