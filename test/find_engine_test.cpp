/**
 * @file find_engine_test.cpp
 * @brief Finding and replacing, as a table of strings.
 *
 * The engine is pure, so everything below is text in, offsets out. One
 * group is not: `OffsetsAreDocumentPositions` puts the same text in a real
 * QTextDocument and checks the engine's indices address the same
 * characters, because every caller treats them as document positions.
 */
#include "ui/editor_tab/find_engine.h"

#include <gtest/gtest.h>

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

using namespace Kites;
using namespace Kites::FindEngine;

namespace
{
FindOptions plain()        { return {}; }
FindOptions caseSensitive(){ FindOptions o; o.caseSensitive = true; return o; }
FindOptions wholeWord()    { FindOptions o; o.wholeWord = true; return o; }
FindOptions regex()        { FindOptions o; o.regex = true; return o; }

/// (start, length) of every match, for compact tables.
QList<QPair<int, int>> spans(const FindResult &result)
{
    QList<QPair<int, int>> out;
    for (const FindMatch &m : result.matches)
        out << qMakePair(m.start, m.length);
    return out;
}

/// Applies every edit back to front, which is what the widget does.
QString applyAll(const QString &text, const QList<ReplaceEdit> &edits)
{
    QString out = text;
    for (int i = edits.size() - 1; i >= 0; --i)
        out.replace(edits.at(i).start, edits.at(i).length, edits.at(i).text);
    return out;
}
} // namespace

TEST(FindEngineBasics, LiteralSearchIsCaseInsensitiveUntilToldOtherwise)
{
    const QString text = "Alpha alpha ALPHA";
    EXPECT_EQ(spans(findAll(text, "alpha", plain())),
              (QList<QPair<int, int>>{{0, 5}, {6, 5}, {12, 5}}));
    EXPECT_EQ(spans(findAll(text, "alpha", caseSensitive())),
              (QList<QPair<int, int>>{{6, 5}}));
}

TEST(FindEngineBasics, AnEmptyPatternFindsNothingAndIsNotAnError)
{
    const FindResult result = findAll("anything at all", "", plain());
    EXPECT_TRUE(result.matches.isEmpty());
    EXPECT_TRUE(result.error.isEmpty());
    EXPECT_FALSE(result.truncated);

    const FindResult asRegex = findAll("anything", "", regex());
    EXPECT_TRUE(asRegex.matches.isEmpty());
    EXPECT_TRUE(asRegex.error.isEmpty());
}

TEST(FindEngineBasics, SpecialCharactersAreLiteralOutsideRegexMode)
{
    const QString text = "a.c abc a+c";
    EXPECT_EQ(spans(findAll(text, "a.c", plain())), (QList<QPair<int, int>>{{0, 3}}));
    EXPECT_EQ(spans(findAll(text, "a.c", regex())),
              (QList<QPair<int, int>>{{0, 3}, {4, 3}, {8, 3}}));
}

TEST(FindEngineBasics, AnInvalidRegexReportsAnErrorAndFindsNothing)
{
    const FindResult result = findAll("abc", "a(", regex());
    EXPECT_TRUE(result.matches.isEmpty());
    EXPECT_FALSE(result.error.isEmpty()) << "an unclosed group produced no error";
    EXPECT_FALSE(result.truncated);

    // The same pattern outside regex mode is just text.
    EXPECT_EQ(spans(findAll("a(b", "a(", plain())), (QList<QPair<int, int>>{{0, 2}}));
}

TEST(FindEngineWholeWord, BoundariesAreNonWordCharactersOnly)
{
    EXPECT_TRUE(isWordCharacter('a'));
    EXPECT_TRUE(isWordCharacter('Z'));
    EXPECT_TRUE(isWordCharacter('7'));
    EXPECT_TRUE(isWordCharacter('_'));
    EXPECT_FALSE(isWordCharacter('-'));
    EXPECT_FALSE(isWordCharacter(' '));

    const QString text = "to total to_x -to- 2to";
    EXPECT_EQ(spans(findAll(text, "to", wholeWord())),
              (QList<QPair<int, int>>{{0, 2}, {15, 2}}))
        << "whole word matched inside a word, or missed one next to punctuation";

    // The same rule applies to a regex pattern.
    FindOptions bothOptions = regex();
    bothOptions.wholeWord   = true;
    EXPECT_EQ(spans(findAll(text, "t.", bothOptions)),
              (QList<QPair<int, int>>{{0, 2}, {15, 2}}));

    // An underscore is a word character, so to_x is one word.
    EXPECT_EQ(spans(findAll("to_x", "to", wholeWord())), (QList<QPair<int, int>>{}));
}

TEST(FindEngineRegex, CaretAndDollarArePerLineAndAMatchMaySpanLines)
{
    const QString text = "one\ntwo\nthree";
    // ^ is zero-width, so ^t is one character long, not two.
    EXPECT_EQ(spans(findAll(text, "^t", regex())),
              (QList<QPair<int, int>>{{4, 1}, {8, 1}}))
        << "^ is not multiline";
    EXPECT_EQ(spans(findAll(text, "o$", regex())), (QList<QPair<int, int>>{{6, 1}}));

    // Across a line break.
    EXPECT_EQ(spans(findAll(text, "one\\ntwo", regex())), (QList<QPair<int, int>>{{0, 7}}));
    EXPECT_EQ(spans(findAll(text, "e.t", regex())), (QList<QPair<int, int>>{}))
        << ". should not cross a newline without DotMatchesEverything";
}

TEST(FindEngineZeroLength, EveryZeroLengthFormIsReportedAndTerminates)
{
    const QString text = "ab\ncd";

    // ^ : one per line start.
    EXPECT_EQ(spans(findAll(text, "^", regex())), (QList<QPair<int, int>>{{0, 0}, {3, 0}}));
    // $ : one per line end.
    EXPECT_EQ(spans(findAll(text, "$", regex())), (QList<QPair<int, int>>{{2, 0}, {5, 0}}));
    // x* : a zero-length match at every position that is not an x.
    const FindResult star = findAll("axxb", "x*", regex());
    EXPECT_FALSE(star.matches.isEmpty());
    EXPECT_TRUE(star.error.isEmpty());
    // \b : word boundaries.
    const FindResult boundary = findAll("ab cd", "\\b", regex());
    EXPECT_EQ(spans(boundary), (QList<QPair<int, int>>{{0, 0}, {2, 0}, {3, 0}, {5, 0}}));

    // The thing that matters most: none of them loops.
    for (const char *pattern : {"^", "$", "x*", "\\b", "(?:)", "a*"})
    {
        const FindResult result = findAll("aaa\nbbb\n", pattern, regex());
        EXPECT_FALSE(result.truncated) << pattern << " ran away to the cap";
        EXPECT_LT(result.matches.size(), 40) << pattern << " reported far too many";
    }
}

TEST(FindEngineZeroLength, NavigationAndReplaceAllHandleThem)
{
    const FindResult result = findAll("ab\ncd", "^", regex());
    ASSERT_EQ(result.matches.size(), 2);

    // Navigation never sticks: from each match the next one moves on.
    EXPECT_EQ(nextIndex(result.matches, 0, true, true), 0);
    EXPECT_EQ(nextIndex(result.matches, 1, true, true), 1);
    EXPECT_EQ(nextIndex(result.matches, 4, true, true), 0) << "forward did not wrap";
    EXPECT_EQ(nextIndex(result.matches, 0, false, true), 1) << "backward did not wrap";

    // Replace All over zero-length matches inserts and terminates.
    const QList<ReplaceEdit> edits = replaceAllEdits(result, "# ", regex());
    EXPECT_EQ(applyAll("ab\ncd", edits), QString("# ab\n# cd"));
}

TEST(FindEngineZeroLength, CommentingEveryLineWithCaretReplaceAll)
{
    // The case from the brief, including a blank line and no trailing one.
    const QString    text   = "addi t0, x0, 5\n\nadd t1, t0, t0";
    const FindResult result = findAll(text, "^", regex());
    EXPECT_EQ(result.matches.size(), 3);
    EXPECT_EQ(applyAll(text, replaceAllEdits(result, "# ", regex())),
              QString("# addi t0, x0, 5\n# \n# add t1, t0, t0"));
}

TEST(FindEngineNavigation, ForwardBackwardAndWrapping)
{
    const FindResult result = findAll("x..x..x", "x", plain());
    ASSERT_EQ(result.matches.size(), 3); // 0, 3, 6

    EXPECT_EQ(nextIndex(result.matches, 0, true, false), 0);
    EXPECT_EQ(nextIndex(result.matches, 1, true, false), 1);
    EXPECT_EQ(nextIndex(result.matches, 7, true, false), -1) << "should not wrap";
    EXPECT_EQ(nextIndex(result.matches, 7, true, true), 0) << "should wrap";
    EXPECT_EQ(nextIndex(result.matches, 0, false, false), -1);
    EXPECT_EQ(nextIndex(result.matches, 0, false, true), 2);
    EXPECT_EQ(nextIndex(result.matches, 4, false, true), 1);

    EXPECT_EQ(nextIndex({}, 0, true, true), -1) << "no matches is not an index";
}

TEST(FindEngineReplacement, PlainIsLiteralAndRegexExpandsGroups)
{
    const FindResult plainResult = findAll("cost", "cost", plain());
    ASSERT_EQ(plainResult.matches.size(), 1);
    EXPECT_EQ(expandReplacement(plainResult.matches.first(), "$1 $$ $0", plain()),
              QString("$1 $$ $0"))
        << "plain mode gave $ a meaning";

    const FindResult grouped = findAll("John Smith", "(\\w+) (\\w+)", regex());
    ASSERT_EQ(grouped.matches.size(), 1);
    const FindMatch &m = grouped.matches.first();
    EXPECT_EQ(m.captures, (QStringList{"John", "Smith"}));
    EXPECT_EQ(expandReplacement(m, "$2, $1", regex()), QString("Smith, John"));
    EXPECT_EQ(expandReplacement(m, "[$0]", regex()), QString("[John Smith]"));
    EXPECT_EQ(expandReplacement(m, "$$5", regex()), QString("$5"));
    EXPECT_EQ(expandReplacement(m, "a$b", regex()), QString("a$b")) << "$ before a letter moved";
    EXPECT_EQ(expandReplacement(m, "end$", regex()), QString("end$")) << "a trailing $ moved";
    EXPECT_EQ(expandReplacement(m, "$7", regex()), QString()) << "a missing group is empty";
}

TEST(FindEngineReplacement, ReplaceAllAppliesBackToFront)
{
    const QString    text   = "a1 a2 a3";
    const FindResult result = findAll(text, "a(\\d)", regex());
    ASSERT_EQ(result.matches.size(), 3);
    EXPECT_EQ(applyAll(text, replaceAllEdits(result, "<$1>", regex())),
              QString("<1> <2> <3>"));

    // A longer replacement than the match, so offsets would drift if the
    // edits were applied front to back.
    EXPECT_EQ(applyAll(text, replaceAllEdits(result, "LONGER", regex())),
              QString("LONGER LONGER LONGER"));
}

TEST(FindEngineCap, TenThousandMatchesAndThenTruncated)
{
    QString text;
    text.reserve(12000);
    for (int i = 0; i < 12000; ++i)
        text += QLatin1Char('x');

    const FindResult result = findAll(text, "x", plain());
    EXPECT_EQ(result.matches.size(), matchCap);
    EXPECT_TRUE(result.truncated);

    // Under the cap, nothing is flagged.
    const FindResult small = findAll("xxx", "x", plain());
    EXPECT_EQ(small.matches.size(), 3);
    EXPECT_FALSE(small.truncated);

    // The cap is a parameter, so the behaviour is testable without 10000.
    const FindResult capped = findAll("xxxxx", "x", plain(), 2);
    EXPECT_EQ(capped.matches.size(), 2);
    EXPECT_TRUE(capped.truncated);
}

TEST(FindEngineOffsets, OffsetsAreDocumentPositions)
{
    // Every caller treats an engine offset as a QTextDocument position.
    // These are the cases where that could go wrong.
    const QStringList documents = {
        "plain text",
        "with\ttabs\tin\tit",
        "empty\n\nline",
        QString::fromUtf8("non \xce\xbb ascii"),
        QString::fromUtf8("emoji \xf0\x9f\x98\x80 here"),
        "no trailing newline",
        "with a trailing newline\n",
        "\n",
        "",
    };

    for (const QString &source : documents)
    {
        QTextDocument document;
        document.setPlainText(source);
        const QString roundTrip = document.toPlainText();
        ASSERT_EQ(roundTrip, source) << "the document changed the text itself";

        // Find every single character and check the document agrees.
        for (const QString &needle : {QStringLiteral("a"), QStringLiteral("\t"),
                                      QStringLiteral("e"), QStringLiteral("n")})
        {
            const FindResult result = findAll(roundTrip, needle, caseSensitive());
            for (const FindMatch &match : result.matches)
            {
                QTextCursor cursor(&document);
                cursor.setPosition(match.start);
                cursor.setPosition(match.end(), QTextCursor::KeepAnchor);
                EXPECT_EQ(cursor.selectedText(), needle)
                    << "offset " << match.start << " in \"" << source.toStdString()
                    << "\" did not select what the engine matched";
            }
        }
    }
}

TEST(FindEngineOffsets, ASurrogatePairCountsAsTwoPositions)
{
    // A 4-byte emoji is two QChars, and a document position counts QChars.
    const QString source = QString::fromUtf8("a\xf0\x9f\x98\x80 b");
    QTextDocument document;
    document.setPlainText(source);
    ASSERT_EQ(document.toPlainText(), source);

    const FindResult result = findAll(document.toPlainText(), "b", caseSensitive());
    ASSERT_EQ(result.matches.size(), 1);
    EXPECT_EQ(result.matches.first().start, 4) << "the emoji was not counted as two";

    QTextCursor cursor(&document);
    cursor.setPosition(result.matches.first().start);
    cursor.setPosition(result.matches.first().end(), QTextCursor::KeepAnchor);
    EXPECT_EQ(cursor.selectedText(), QString("b"));
}
