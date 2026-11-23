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

using namespace Atlas;
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

// The highlighter cannot be checked by RENDERING here: atlas_test loads
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

// ════════════ the index abandons a line the lexer abandons ══════════════
//
// Lexer::getNextToken() ends in `else { skipLine(); return INVALID; }`:
// any character it has no case for discards the REST OF THE LINE. `'` is
// one of them, and so are '%', '@', '+' and '['. A first attempt at this
// merely stopped treating `'` as a string quote, which made the index
// carry on scanning past it - less faithful than before, not more, and no
// test noticed. Its control did not bite, which is what found it.

TEST(CommentSyntaxIndex, AnUnknownCharacterEndsTheLineForTheIndexToo)
{
    // Line 3 is discarded by the lexer, so `loop` there is not a
    // reference. Only the one on line 2 is.
    const QString text = "loop:\n    j loop\n    ' j loop\n";
    const LabelIndex::Index index = LabelIndex::findLabels(text);
    int refs = 0;
    for (const LabelIndex::Reference &reference : index.references)
        if (reference.name == QLatin1String("loop"))
            ++refs;
    EXPECT_EQ(refs, 1) << "the index scanned past a character the lexer gives up on";
}

TEST(CommentSyntaxIndex, TheSameHoldsForTheOtherCharactersTheLexerDropsOn)
{
    for (const QString &bad : {QStringLiteral("'"), QStringLiteral("%"),
                               QStringLiteral("@"), QStringLiteral("+"),
                               QStringLiteral("[")})
    {
        const QString text =
            QStringLiteral("loop:\n    %1 j loop\n").arg(bad);
        const LabelIndex::Index index = LabelIndex::findLabels(text);
        EXPECT_TRUE(index.references.isEmpty())
            << "'" << bad.toStdString() << "' did not end the line for the index";
    }
}

TEST(CommentSyntaxIndex, ADefinitionAfterSuchACharacterIsNotALabel)
{
    const QString text = "main:\n    nop\n    ' ghost:\n";
    const LabelIndex::Index index = LabelIndex::findLabels(text);
    EXPECT_FALSE(index.isDefined(QStringLiteral("ghost")))
        << "a name the lexer never reaches was taken as a definition";
    EXPECT_TRUE(index.isDefined(QStringLiteral("main")));
}

// ═════════════ the alphabet, measured against the real lexer ════════════
//
// atlas-verify/harness/alphabetprobe asks the real lexer, for all 95
// printable ASCII codes plus \t \r \f \v and five non-ASCII, whether it
// carries on past the character or abandons the line, and compares that
// with what the index believes. It found no character where the index
// abandons and the lexer carries on - no missed reference, so no rename
// defect - and four where the index carries on and the lexer abandons.

TEST(CommentSyntaxAlphabet, ABareColonEndsTheLine)
{
    // ':' reaches getNextToken()'s dispatch only when it is NOT attached
    // to an identifier - identifier() swallows the attached case itself -
    // and the dispatch has no branch for it, so it goes to skipLine().
    // isLexerCharacter() allowed ':' unconditionally.
    const QString text = "loop:\n    : j loop\n";
    const LabelIndex::Index index = LabelIndex::findLabels(text);
    EXPECT_TRUE(index.references.isEmpty())
        << "a reference was reported after a bare ':', which the lexer never reaches";
}

TEST(CommentSyntaxAlphabet, AColonAttachedToANameStillDefinesALabel)
{
    // The other half: the attached ':' must keep working, or every label
    // definition disappears.
    const QString text = "loop:\n    j loop\nsecond:\n    j second\n";
    const LabelIndex::Index index = LabelIndex::findLabels(text);
    EXPECT_TRUE(index.isDefined(QStringLiteral("loop")));
    EXPECT_TRUE(index.isDefined(QStringLiteral("second")));
    EXPECT_EQ(index.references.size(), 2);
}

TEST(CommentSyntaxAlphabet, NonAsciiLettersEndTheLine)
{
    // The lexer tests bytes with std::isalpha. A UTF-8 'e-acute' is two
    // bytes, neither of which is an ASCII letter, so the dispatch falls
    // through to skipLine(). QChar::isLetterOrNumber() says otherwise,
    // which is what the index was using.
    for (const QString &c : {QString(QChar(0x00E9)),   // e with an acute
                             QString(QChar(0x4E2D)),   // a Chinese character
                             QString(QChar(0x00A0))})  // no-break space
    {
        const QString text = QStringLiteral("loop:\n    %1 j loop\n").arg(c);
        const LabelIndex::Index index = LabelIndex::findLabels(text);
        EXPECT_TRUE(index.references.isEmpty())
            << "U+" << QString::number(c.at(0).unicode(), 16).toStdString()
            << " did not end the line for the index";
    }
}

TEST(CommentSyntaxAlphabet, AnEmojiEndsTheLineToo)
{
    const QString emoji = QString::fromUcs4(U"\U0001F600");
    const QString text  = QStringLiteral("loop:\n    %1 j loop\n").arg(emoji);
    EXPECT_TRUE(LabelIndex::findLabels(text).references.isEmpty());
}

TEST(CommentSyntaxAlphabet, AsciiLettersAndDigitsStillScan)
{
    // The guard against over-correcting: restricting to ASCII must not
    // stop the ordinary alphabet working.
    const QString text = "loop_9:\n    j loop_9\n    beq x1, x2, loop_9\n";
    const LabelIndex::Index index = LabelIndex::findLabels(text);
    EXPECT_TRUE(index.isDefined(QStringLiteral("loop_9")));
    EXPECT_EQ(index.references.size(), 2);
}
