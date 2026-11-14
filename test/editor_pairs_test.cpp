/**
 * @file editor_pairs_test.cpp
 * @brief Auto-closing pairs, as a table of strings.
 *
 * Pure: document text and a selection in, one Edit out. The rules are
 * driven by the typed TEXT rather than Qt::Key, so a layout where `(` is
 * not Shift+9 behaves identically, and the table reflects that by typing
 * strings.
 */
#include "ui/editor_tab/editor_commands.h"

#include "ui/editor_tab/main_editor.h"
#include <QApplication>
#include <QTextDocument>
#include <gtest/gtest.h>

#include <QList>
#include <QString>

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

struct Case
{
    const char *name;
    QString     before;
    int         anchor;
    int         position;
    const char *typed;
    QString     after;      // the document; same as `before` when nothing applies
    int         newAnchor;
    int         newPosition;
};

void runTable(const QList<Case> &rows)
{
    for (const Case &row : rows)
    {
        const Edit edit = autoPair(row.before, row.anchor, row.position, row.typed);
        EXPECT_EQ(applyTo(row.before, edit), row.after) << "text: " << row.name;
        EXPECT_EQ(edit.valid ? edit.anchor : row.anchor, row.newAnchor)
            << "anchor: " << row.name;
        EXPECT_EQ(edit.valid ? edit.position : row.position, row.newPosition)
            << "position: " << row.name;
    }
}

/// Types `keys` one character at a time, applying the pair rule where it
/// applies and inserting the character where it does not - which is what
/// the editor does.
QPair<QString, int> typeAll(const QString &keys)
{
    QString text;
    int     caret = 0;
    for (const QChar &key : keys)
    {
        const Edit edit = autoPair(text, caret, caret, QString(key));
        if (edit.valid)
        {
            text.replace(edit.start, edit.length, edit.text);
            caret = edit.position;
        }
        else
        {
            text.insert(caret, key);
            ++caret;
        }
    }
    return {text, caret};
}
} // namespace

TEST(EditorPairsOpening, AnOpenerClosesItselfOnlyWhereThereIsRoom)
{
    runTable({
        {"( at the end of a line", "", 0, 0, "(", "()", 1, 1},
        // caret 1 is between "a" and the space, so the next character is
        // the space and the pair is wanted. (At caret 2 the next character
        // is "b" and it is not - the row below.)
        {"( before a space", "a b", 1, 1, "(", "a() b", 2, 2},
        {"( before a letter after a space", "a b", 2, 2, "(", "a b", 2, 2},
        {"( before a closing paren", "x)", 1, 1, "(", "x())", 2, 2},
        {"( before a word character", "abc", 0, 0, "(", "abc", 0, 0},
        {"( before a digit", "4(sp)", 0, 0, "(", "4(sp)", 0, 0},
        {"\" at the end of a line", "", 0, 0, "\"", "\"\"", 1, 1},
        {"' at the end of a line", "", 0, 0, "'", "''", 1, 1},
    });
}

TEST(EditorPairsOpening, AQuoteRefusesAfterAWordCharacterAndInAComment)
{
    runTable({
        // don't
        {"' after a letter", "don", 3, 3, "'", "don", 3, 3},
        {"' after a digit", "x9", 2, 2, "'", "x9", 2, 2},
        {"' after an underscore", "a_", 2, 2, "'", "a_", 2, 2},
        {"' after a space still pairs", "a ", 2, 2, "'", "a ''", 3, 3},
        // inside a comment nothing pairs
        {"\" inside a comment", "# say ", 6, 6, "\"", "# say ", 6, 6},
        {"( inside a comment", "# note ", 7, 7, "(", "# note ", 7, 7},
        {"' inside a comment", "# it", 4, 4, "'", "# it", 4, 4},
        // a # inside a string is not a comment
        {"( after a # that is inside a string", "\"a# \"", 4, 4, "(", "\"a# \"", 4, 4},
    });
}

TEST(EditorPairsOpening, ParenthesesDoNotPairInsideAString)
{
    runTable({
        {"( inside a string", ".asciz \"a \"", 10, 10, "(", ".asciz \"a \"", 10, 10},
        {"( after the string has closed", ".asciz \"ab\" ", 12, 12, "(", ".asciz \"ab\" ()",
         13, 13},
        {"an escaped quote does not close the string", ".asciz \"a\\\" \"", 12, 12, "(",
         ".asciz \"a\\\" \"", 12, 12},
    });
}

TEST(EditorPairsClosing, TypingTheCloserStepsOverIt)
{
    runTable({
        {") over a )", "()", 1, 1, ")", "()", 2, 2},
        {"\" over a \"", "\"\"", 1, 1, "\"", "\"\"", 2, 2},
        {"' over a '", "''", 1, 1, "'", "''", 2, 2},
        // Not over something else.
        {") in front of a letter", "(a)", 1, 1, ")", "(a)", 1, 1},
        {") at the end of a line", "(", 1, 1, ")", "(", 1, 1},
    });
}

TEST(EditorPairsSelection, AnOpenerWrapsASelectionAndKeepsItSelected)
{
    runTable({
        {"( wraps a word", "alpha beta", 0, 5, "(", "(alpha) beta", 1, 6},
        {"\" wraps a word", "alpha", 0, 5, "\"", "\"alpha\"", 1, 6},
        {"backwards selection wraps the same", "alpha", 5, 0, "(", "(alpha)", 1, 6},
        // A closer with a selection is just a character.
        {") with a selection does nothing special", "alpha", 0, 5, ")", "alpha", 0, 5},
    });

    // A multi-line selection wraps too, and carries a line map so the
    // breakpoints are handled like every other command.
    const QString text = "one\ntwo\nthree";
    const Edit    edit = autoPair(text, 0, 7, "(");
    ASSERT_TRUE(edit.valid);
    EXPECT_EQ(applyTo(text, edit), QString("(one\ntwo)\nthree"));
    EXPECT_EQ(edit.lines.firstLine, 0);
    EXPECT_EQ(edit.lines.lastLine, 1);
    for (int line = 0; line <= 2; ++line)
        EXPECT_EQ(edit.lines.mapped(line), line)
            << "wrapping a selection moved line " << line;
}

TEST(EditorPairsBackspace, BetweenAnEmptyPairBothGo)
{
    struct Row { const char *name; QString text; int caret; QString after; int newCaret; };
    const QList<Row> rows = {
        {"()", "()", 1, "", 0},
        {"\"\"", "\"\"", 1, "", 0},
        {"''", "''", 1, "", 0},
        {"inside a line", "a()b", 2, "ab", 1},
    };
    for (const Row &row : rows)
    {
        const Edit edit = pairBackspace(row.text, row.caret, row.caret);
        ASSERT_TRUE(edit.valid) << row.name;
        EXPECT_EQ(applyTo(row.text, edit), row.after) << row.name;
        EXPECT_EQ(edit.position, row.newCaret) << row.name;
    }

    // Everything else is an ordinary Backspace.
    for (const auto &[text, caret] : QList<QPair<QString, int>>{
             {"(x)", 2}, {"()", 0}, {"()", 2}, {"ab", 1}, {"", 0}, {"(]", 1}})
        EXPECT_FALSE(pairBackspace(text, caret, caret).valid)
            << "\"" << text.toStdString() << "\" at " << caret;

    // With a selection it is never the pair rule.
    EXPECT_FALSE(pairBackspace("()", 0, 2).valid);
}

TEST(EditorPairsTyping, TypingRealAssemblyCharacterByCharacter)
{
    // The case from the brief: every character of a load, typed in order.
    // "4(" does NOT pair, because a digit is a word character - which is
    // what keeps `4(sp)` from becoming `4()sp)`.
    const auto load = typeAll("lw t0, 4(sp)");
    EXPECT_EQ(load.first, QString("lw t0, 4(sp)"));
    EXPECT_EQ(load.second, 12);

    // A string with a parenthesis inside it.
    const auto asciz = typeAll(".asciz \"a(b\"");
    EXPECT_EQ(asciz.first, QString(".asciz \"a(b\""));

    // An apostrophe in a comment.
    const auto comment = typeAll("# it's");
    EXPECT_EQ(comment.first, QString("# it's"));

    // And one where the pair IS wanted: a call with nothing after it.
    const auto call = typeAll("jal ra, f");
    EXPECT_EQ(call.first, QString("jal ra, f"));
    const auto empty = typeAll("(");
    EXPECT_EQ(empty.first, QString("()"));
    EXPECT_EQ(empty.second, 1);
}

TEST(EditorPairsBreakpoints, OnlyAMultiLineWrapCarriesALineMap)
{
    // Every single-character case leaves the lines alone, so the default
    // identity map is right and no breakpoint moves.
    const QStringList documents = {"", "abc", "a b", "()", "\"\"", "# x"};
    for (const QString &text : documents)
        for (const char *typed : {"(", ")", "\"", "'"})
        {
            const Edit edit = autoPair(text, 0, 0, typed);
            for (int line = 0; line < 3; ++line)
                EXPECT_EQ(edit.lines.mapped(line), line)
                    << "typing " << typed << " in \"" << text.toStdString()
                    << "\" moved line " << line;
        }

    // Backspace over a pair likewise.
    const Edit back = pairBackspace("a\n()\nb", 3, 3);
    ASSERT_TRUE(back.valid);
    for (int line = 0; line < 3; ++line)
        EXPECT_EQ(back.lines.mapped(line), line);

    // The one case that rewrites more than one line: wrapping a selection
    // that spans lines. It names the span it rewrote, like every other
    // command, and the map is the IDENTITY over it - the quotes go INSIDE
    // the span, so no line is created, destroyed or moved and a breakpoint
    // on any of them stays where it is.
    //
    // Asserting the span and every entry, not merely that a map exists: a
    // map that collapsed the span onto its first line would satisfy a
    // weaker check and silently drag every breakpoint in the selection up
    // to one line. A control that made exactly that mutation is what found
    // this test asserting less than its name.
    const QString   multi = "one\ntwo\nthree\nfour";
    const Edit      wrap  = autoPair(multi, 4, 13, "(");     // "two\nthree"
    ASSERT_TRUE(wrap.valid);
    EXPECT_EQ(wrap.lines.firstLine, 1);
    EXPECT_EQ(wrap.lines.lastLine, 2);
    for (int line = 0; line < 4; ++line)
        EXPECT_EQ(wrap.lines.mapped(line), line) << "wrapping moved line " << line;
}

// ═══════════════════════════ bracket matching ═════════════════════════════

namespace
{
QPair<int, int> bracketOf(const QString &text, int position)
{
    const BracketPair pair = bracketAt(text, position);
    return {pair.offset, pair.partner};
}
} // namespace

TEST(EditorBrackets, TheCaretBesideABracketFindsItsPartner)
{
    //                            0123456789...
    const QString load = "lw t0, 4(sp)";
    EXPECT_EQ(bracketOf(load, 8), qMakePair(8, 11)) << "caret before the (";
    EXPECT_EQ(bracketOf(load, 9), qMakePair(8, 11)) << "caret after the (";
    EXPECT_EQ(bracketOf(load, 11), qMakePair(11, 8)) << "caret before the )";
    EXPECT_EQ(bracketOf(load, 12), qMakePair(11, 8)) << "caret after the )";

    // Not beside one at all: nothing, which is the common case.
    EXPECT_EQ(bracketOf(load, 0), qMakePair(-1, -1));
    EXPECT_EQ(bracketOf(load, 5), qMakePair(-1, -1));

    // Nested.
    const QString nested = "((a))";
    EXPECT_EQ(bracketOf(nested, 0), qMakePair(0, 4));
    EXPECT_EQ(bracketOf(nested, 1), qMakePair(1, 3));
}

TEST(EditorBrackets, AnUnmatchedBracketHasNoPartner)
{
    EXPECT_EQ(bracketOf("lw t0, 4(sp", 8), qMakePair(8, -1));
    EXPECT_EQ(bracketOf("a)b", 1), qMakePair(1, -1));
    EXPECT_EQ(bracketOf("(", 0), qMakePair(0, -1));
}

TEST(EditorBrackets, TheSearchStopsAtTheEndOfTheLine)
{
    // A RISC-V parenthesis is an addressing mode and never spans lines, so
    // the partner on another line is deliberately NOT found.
    const QString twoLines = "foo(\nbar)";
    EXPECT_EQ(bracketOf(twoLines, 3), qMakePair(3, -1))
        << "the search crossed a line break";
    EXPECT_EQ(bracketOf(twoLines, 8), qMakePair(8, -1));

    // On one line it is found.
    EXPECT_EQ(bracketOf("foo(bar)", 3), qMakePair(3, 7));
}

TEST(EditorBrackets, CommentsAndStringsAreIgnoredOnBothEnds)
{
    // A bracket in a comment is not a bracket.
    EXPECT_EQ(bracketOf("# a (b", 4), qMakePair(-1, -1));
    // Nor one in a string.
    EXPECT_EQ(bracketOf(".asciz \"a(b\"", 9), qMakePair(-1, -1));
    // And a real bracket is not matched to one inside a string.
    EXPECT_EQ(bracketOf("f( \")\" )", 1), qMakePair(1, 7))
        << "the ) inside the string was taken as the partner";
    // Nor to one inside a comment.
    EXPECT_EQ(bracketOf("f( # )", 1), qMakePair(1, -1));
}

/**
 * @brief Block-relative and whole-document calls must agree.
 *
 * updateBracketHighlight() hands bracketAt() the caret's BLOCK and adds the
 * block's offset back on, because the function never looks past the end of
 * a line anyway. That is only safe if the two calls give the same answer,
 * which is what this asserts - over a table of lines chosen for the cases
 * where "the line" and "the document" differ: the first line, the last line
 * with no trailing newline, an empty line, a line whose brackets are inside
 * a comment or a string, and a bracket whose partner is on another line.
 */
TEST(EditorBrackets, ABlockRelativeCallAgreesWithAWholeDocumentOne)
{
    const QStringList lines = {
        "lw t0, 4(sp)",        // 0: a plain pair
        "",                    // 1: empty
        "addi t1, t1, 1",      // 2: no brackets
        "sw t2, (",            // 3: an opener whose partner is on no line
        ")  # )",              // 4: a closer, and one in a comment
        "  .asciz \"a(b\"",    // 5: a bracket inside a string
        "jal ra, f(x)(y)",     // 6: two pairs on one line
        "lb t3, 0(t4)"         // 7: last line, no trailing newline
    };
    const QString document = lines.join('\n');

    // Every caret position of every line, both ways.
    int base = 0;
    for (int line = 0; line < lines.size(); ++line)
    {
        const QString &text = lines.at(line);
        for (int offset = 0; offset <= text.size(); ++offset)
        {
            const BracketPair fromBlock = bracketAt(text, offset);
            const BracketPair fromWhole = bracketAt(document, base + offset);

            const int blockOffset  = fromBlock.offset < 0 ? -1 : fromBlock.offset + base;
            const int blockPartner = fromBlock.partner < 0 ? -1 : fromBlock.partner + base;

            EXPECT_EQ(blockOffset, fromWhole.offset)
                << "line " << line << " offset " << offset << " (\""
                << text.toStdString() << "\")";
            EXPECT_EQ(blockPartner, fromWhole.partner)
                << "line " << line << " offset " << offset << " (\""
                << text.toStdString() << "\")";
        }
        base += text.size() + 1;
    }
}

/**
 * @brief The highlight lands on the right characters, not the right offsets
 *        of some other line.
 *
 * updateBracketHighlight() asks bracketAt() about the caret's BLOCK and
 * then adds the block's position back on. Forgetting that addition leaves
 * the selection on line 1 whatever line the caret is on - and the pure
 * function's own tests cannot see it, because they never go through the
 * widget. This one does.
 */
TEST(EditorBracketsWiring, TheHighlightLandsOnTheCaretsOwnLine)
{
    Editor editor;
    editor.resize(600, 400);
    editor.show();
    // Line 0 also has a pair, so an un-shifted offset would still find
    // SOMETHING - and land on the wrong line, which is the bug.
    editor.setPlainText("lw t0, 4(sp)\nnop\nsw t1, 8(sp)\nnop");
    QApplication::processEvents();

    const int target = editor.toPlainText().indexOf("(sp)", 10);
    ASSERT_GT(target, 20);

    QTextCursor beside(editor.document());
    beside.setPosition(target + 1); // just after the '('
    editor.setTextCursor(beside);
    QApplication::processEvents();

    QList<int> singles;
    for (const QTextEdit::ExtraSelection &selection : editor.extraSelections())
    {
        const int start = selection.cursor.selectionStart();
        const int end   = selection.cursor.selectionEnd();
        if (end - start == 1)
            singles << start;
    }
    EXPECT_TRUE(singles.contains(target))
        << "the opening bracket on the caret's own line was not highlighted";
    EXPECT_TRUE(singles.contains(target + 3))
        << "the partner on the caret's own line was not highlighted";
    for (int offset : singles)
        EXPECT_GE(offset, 13) << "a highlight landed on line 1, where the caret is not";
}
