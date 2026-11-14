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
