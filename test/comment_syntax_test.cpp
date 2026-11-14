/**
 * @file comment_syntax_test.cpp
 * @brief Every component that decides "is this a comment" agrees with the lexer.
 *
 * `Lexer::getNextToken()` treats BOTH '#' and ';' as starting a comment,
 * at any column, including immediately after a token. It has no
 * single-quoted strings (a `'` falls through to skipLine()) and no
 * backslash escaping inside a `"..."` string. Those facts were measured
 * through the real lexer, not read off the source — see
 * `atlas-verify/harness/commentprobe`.
 *
 * The editor had four separate answers to the same question, and three of
 * them knew only about '#'. These are the GUARDs that keep them together.
 * The lexer itself is read, never modified.
 */
#include "ui/common/syntax_highlighter.h"
#include "ui/editor_tab/editor_commands.h"
#include "ui/editor_tab/label_index.h"

#include <gtest/gtest.h>

#include <QRegularExpression>

using namespace Kites;
using EditorCommands::contextAt;
using EditorCommands::LineContext;

// ══════════════════════ contextAt: the shared scanner ═══════════════════

TEST(CommentSyntaxContext, ASemicolonStartsAComment)
{
    const QString line = "    j loop  ; j other";
    EXPECT_FALSE(contextAt(line, 6).inComment) << "the code half was called a comment";
    EXPECT_TRUE(contextAt(line, line.indexOf("j other")).inComment)
        << "the text after ';' was not treated as a comment";
}

TEST(CommentSyntaxContext, ASemicolonAtColumnZeroCommentsTheWholeLine)
{
    const QString line = "; j other";
    EXPECT_TRUE(contextAt(line, 5).inComment);
}

TEST(CommentSyntaxContext, ASemicolonRightAfterATokenStillComments)
{
    // Measured: `j loop; j other` yields OPCODE(j) LABEL_REF(loop) and
    // nothing else - no space is needed before the ';'.
    const QString line = "    j loop; j other";
    EXPECT_TRUE(contextAt(line, line.indexOf("j other")).inComment);
}

TEST(CommentSyntaxContext, BothCommentCharactersWinOverAQuote)
{
    // Measured: `j loop  ; "unclosed` lexes as OPCODE LABEL_REF and the
    // rest is comment - the quote does not open a string inside one.
    for (const QString &line : {QStringLiteral("    j loop  # \"unclosed"),
                                QStringLiteral("    j loop  ; \"unclosed")})
    {
        const LineContext context = contextAt(line, line.size());
        EXPECT_TRUE(context.inComment) << line.toStdString();
        EXPECT_FALSE(context.inString) << line.toStdString();
    }
}

TEST(CommentSyntaxContext, AQuotedStringHidesBothCommentCharacters)
{
    // Measured: STRING(a # b) and STRING(a ; b). Inside a "..." neither
    // character starts anything.
    for (const QString &line : {QStringLiteral("    .string \"a # b\""),
                                QStringLiteral("    .string \"a ; b\"")})
    {
        const int at = line.indexOf(QLatin1Char('b'));
        const LineContext context = contextAt(line, at);
        EXPECT_TRUE(context.inString) << line.toStdString();
        EXPECT_FALSE(context.inComment) << line.toStdString();
    }
}

TEST(CommentSyntaxContext, ThereAreNoSingleQuotedStrings)
{
    // Measured: `.string 'a # b'` lexes as DIRECTIVE(string) INVALID().
    // The lexer has no case for '\'' at all, so it cannot open a string -
    // which means the '#' after it IS a comment.
    const QString line = "    .string 'a # b'";
    const LineContext context = contextAt(line, line.indexOf(QLatin1Char('b')));
    EXPECT_FALSE(context.inString) << "a single-quoted run was treated as a string";
    EXPECT_TRUE(context.inComment) << "the '#' after it did not start a comment";
}

TEST(CommentSyntaxContext, ABackslashDoesNotEscapeAQuote)
{
    // Measured: `.string "a\"b"` lexes as STRING(a\) INVALID(b) INVALID().
    // The string ENDS at the quote after the backslash, so what follows is
    // code, and a '#' there is a comment.
    // `.string "a\"b" # tail` : the string ends at the quote AFTER the
    // backslash, so `b` is code. The quote that follows then opens a
    // second, unterminated string - the lexer says so out loud
    // ("Unterminated string literal") - so the '#' after it is inside
    // that string and is NOT a comment. Both halves are the lexer's.
    const QString escaped = "    .string \"a\\\"b\" # tail";
    EXPECT_FALSE(contextAt(escaped, escaped.indexOf(QLatin1Char('b'))).inString)
        << "the backslash was treated as an escape, which the lexer does not do";
    EXPECT_TRUE(contextAt(escaped, escaped.indexOf(QStringLiteral("tail"))).inString)
        << "the unterminated string after the escaped quote was not tracked";

    // With no second opening quote the tail really is a comment.
    const QString closed = "    .string \"a\\\" # tail";
    EXPECT_TRUE(contextAt(closed, closed.indexOf(QStringLiteral("tail"))).inComment)
        << "after the string closed, the '#' did not start a comment";
}

// ═════════════════ what inherits contextAt's answer ═════════════════════

TEST(CommentSyntaxPairs, NoAutoPairInsideASemicolonComment)
{
    for (const QString &typed : {QStringLiteral("("), QStringLiteral("\"")})
    {
        const QString line = "    j loop  ; note ";
        const EditorCommands::Edit edit =
            EditorCommands::autoPair(line, line.size(), line.size(), typed);
        EXPECT_FALSE(edit.valid)
            << "typing " << typed.toStdString() << " inside a ';' comment paired";
    }
}

TEST(CommentSyntaxPairs, BracketsInsideASemicolonCommentDoNotMatch)
{
    const QString line = "    j loop  ; (x)";
    const int     at   = line.indexOf(QLatin1Char('('));
    const EditorCommands::BracketPair pair = EditorCommands::bracketAt(line, at);
    EXPECT_EQ(pair.offset, -1)
        << "a bracket inside a ';' comment was matched as code";
}

// ═══════════════════════ toggleComment ══════════════════════════════════

TEST(CommentSyntaxToggle, ItUncommentsALineThatUsesASemicolon)
{
    // A ';' comment is a comment, so the toggle must take it off. It
    // INSERTS '#', which is what the rest of the corpus uses, so a
    // round trip on a ';' line normalises it rather than preserving it.
    const QString text = "; j loop\n";
    const EditorCommands::Edit edit = EditorCommands::toggleComment(text, 0, 0);
    ASSERT_TRUE(edit.valid);
    const QString after = QString(text).replace(edit.start, edit.length, edit.text);
    EXPECT_EQ(after, QStringLiteral("j loop\n"))
        << "a ';'-commented line was commented again instead of uncommented";
}

TEST(CommentSyntaxToggle, AMixedBlockIsTreatedAsAlreadyCommented)
{
    const QString text = "# one\n; two\n";
    const EditorCommands::Edit edit = EditorCommands::toggleComment(text, 0, text.size() - 1);
    ASSERT_TRUE(edit.valid);
    const QString after = QString(text).replace(edit.start, edit.length, edit.text);
    EXPECT_EQ(after, QStringLiteral("one\ntwo\n"))
        << "a block of '#' and ';' comments was not recognised as commented";
}

TEST(CommentSyntaxToggle, ItStillInsertsAHash)
{
    const QString text = "    j loop\n";
    const EditorCommands::Edit edit = EditorCommands::toggleComment(text, 0, 0);
    ASSERT_TRUE(edit.valid);
    const QString after = QString(text).replace(edit.start, edit.length, edit.text);
    EXPECT_EQ(after, QStringLiteral("    # j loop\n"))
        << "the inserted comment character changed";
}

// ═══════════════════════ the syntax highlighter ═════════════════════════

// The highlighter cannot be checked by RENDERING here: kites_test loads
// no theme resources, so ThemeManager::getSyntaxFormat() returns an empty
// QTextCharFormat, setFormat() with an empty format adds no range, and
// block.layout()->formats() comes back empty for every line. A colour
// comparison passes whatever the rules say, and a coverage check fails
// even for '#'. Both were tried; both are recorded in known_issues.
//
// What IS testable without a theme is the RULE. SyntaxHighlighter exposes
// the comment pattern it installs, so the test and the highlighter share
// one definition and cannot drift. The rendering is verified in the
// out-of-tree harness, which has the theme resources.

TEST(CommentSyntaxHighlighter, TheCommentRuleCoversBothCharacters)
{
    const QRegularExpression pattern = SyntaxHighlighter::commentPattern();

    struct Case { const char *line; const char *mustCover; bool covered; };
    const Case cases[] = {
        {"    j loop  # note", "note", true},
        {"    j loop  ; note", "note", true},
        {"# whole line",       "whole", true},
        {"; whole line",       "whole", true},
        {"    j loop; tight",  "tight", true},
        {"    j loop# tight",  "tight", true},
        // Inside a "..." neither character starts a comment - measured on
        // the real lexer, which yields STRING(a # b) and STRING(a ; b).
        {"    .string \"a # b\"", "b\"", false},
        {"    .string \"a ; b\"", "b\"", false},
    };

    for (const Case &c : cases)
    {
        const QString line = QString::fromUtf8(c.line);
        const int     at   = line.indexOf(QString::fromUtf8(c.mustCover));
        ASSERT_GE(at, 0) << c.line;
        bool covered = false;
        auto it = pattern.globalMatch(line);
        while (it.hasNext())
        {
            const QRegularExpressionMatch m = it.next();
            // Group 1, when it took part, is a string that the rule
            // matched only in order to step over it.
            if (m.capturedStart(QStringLiteral("string")) >= 0)
                continue;
            if (at >= m.capturedStart() && at < m.capturedEnd())
                covered = true;
        }
        EXPECT_EQ(covered, c.covered)
            << c.line << " : expected '" << c.mustCover
            << "' to be " << (c.covered ? "inside" : "outside") << " a comment";
    }
}
