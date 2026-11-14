/**
 * @file completion_model_test.cpp
 * @brief The suggestion matcher, as a table. No widget, no cursor.
 *
 * Every one of these is a GUARD on the ranking rule: the popup shows the top
 * of this list, so a change here is a change to what the first Enter inserts.
 */
#include "ui/editor_tab/completion_model.h"

#include <gtest/gtest.h>

using namespace Kites::Completion;

namespace
{
QList<Item> itemsOf(const QStringList &labels)
{
    QList<Item> items;
    for (const QString &label : labels)
        items << Item{label, kindOf(label), {}};
    return items;
}

/// The labels that survive `pattern`, in rank order.
QStringList ranked(const QStringList &labels, const QString &pattern)
{
    const QList<Item> items = itemsOf(labels);
    QStringList       out;
    for (const Match &m : filter(items, pattern))
        out << items.at(m.index).label;
    return out;
}

const QStringList riscv = {"add",  "addi", "addiw", "addw", "and", "andi",
                           "sub",  "subw", "sb",    "sd",   "sh",  "sw",
                           "fadd.s", "fadd.d", "fsub.s",
                           "x1",   "x10",  "x11",   "x12",  "x13", "x14",
                           "x15",  "x16",  "x17",   "x18",  "x19",
                           ".word", ".globl", "loop", "loop_end", "main"};
} // namespace

TEST(CompletionScorer, APrefixBeatsEverythingElse)
{
    const QStringList out = ranked(riscv, "ad");
    ASSERT_FALSE(out.isEmpty());
    // "ad" is a prefix of add/addi/addiw/addw; shortest first inside the band.
    EXPECT_EQ(out.at(0), QString("add"));
    EXPECT_EQ(out.at(1), QString("addi"));
    EXPECT_TRUE(out.contains("addw"));
    EXPECT_TRUE(out.contains("addiw"));
    // "and" does NOT contain d after n... it does: a-n-d. It matches in order,
    // so it is present, but below every prefix match.
    EXPECT_GT(out.indexOf("and"), out.indexOf("addw"));
}

TEST(CompletionScorer, SkippedCharactersStillMatchInOrder)
{
    // The headline fuzzy case: "adi" is not a prefix of anything.
    const QStringList out = ranked(riscv, "adi");
    ASSERT_FALSE(out.isEmpty());
    EXPECT_EQ(out.at(0), QString("addi"));
    EXPECT_TRUE(out.contains("addiw"));
    EXPECT_FALSE(out.contains("add")) << "add has no 'i'";
}

TEST(CompletionScorer, RegisterNumbersSortShortestFirst)
{
    const QStringList out = ranked(riscv, "x1");
    ASSERT_FALSE(out.isEmpty());
    EXPECT_EQ(out.at(0), QString("x1")) << "the exact prefix of its own length comes first";
    // x10..x19 follow, in order.
    const QStringList expected = {"x1", "x10", "x11", "x12", "x13",
                                  "x14", "x15", "x16", "x17", "x18", "x19"};
    EXPECT_EQ(out, expected);
}

TEST(CompletionScorer, AMatchMayStartAfterADotOrUnderscore)
{
    // 's' starts the segment after the '.', so fadd.s matches "s".
    EXPECT_TRUE(ranked({"fadd.s"}, "s").contains("fadd.s"));
    EXPECT_TRUE(ranked({"loop_end"}, "e").contains("loop_end"));
    // but not from the middle of a segment
    EXPECT_TRUE(ranked({"fadd.s"}, "d").isEmpty())
        << "'d' is inside the first segment, not the start of one";
}

TEST(CompletionScorer, NoMatchReturnsNothing)
{
    EXPECT_TRUE(ranked(riscv, "zzz").isEmpty());
    EXPECT_TRUE(ranked(riscv, "qqqqqq").isEmpty());
    const Match m = score("addi", "zzz");
    EXPECT_EQ(m.score, 0);
    EXPECT_TRUE(m.matched.isEmpty());
}

TEST(CompletionScorer, AnEmptyPatternMatchesEverythingAlphabetically)
{
    // What Ctrl+Space with no prefix shows.
    const QStringList out = ranked(riscv, "");
    EXPECT_EQ(out.size(), riscv.size());
    for (int i = 1; i < out.size(); ++i)
        EXPECT_LE(out.at(i - 1).size() != out.at(i).size()
                      ? 0
                      : out.at(i - 1).compare(out.at(i), Qt::CaseInsensitive),
                  0);
}

TEST(CompletionScorer, TheMatchedIndicesAreTheCharactersToHighlight)
{
    const Match m = score("addi", "adi");
    ASSERT_EQ(m.matched.size(), 3);
    EXPECT_EQ(m.matched.at(0), 0); // a
    EXPECT_EQ(m.matched.at(1), 1); // d  (the first of the two)
    EXPECT_EQ(m.matched.at(2), 3); // i
    const Match p = score("add", "add");
    EXPECT_EQ(p.matched, QList<int>({0, 1, 2}));
}

TEST(CompletionScorer, ExactCaseBeatsTheSameWordInAnotherCase)
{
    const QStringList out = ranked({"Loop", "loop"}, "loop");
    ASSERT_EQ(out.size(), 2);
    EXPECT_EQ(out.at(0), QString("loop")) << "the exact-case prefix ranks first";
}

TEST(CompletionScorer, APrefixOfAnotherLabelDoesNotSwallowIt)
{
    const QStringList out = ranked({"loop", "loop2", "loop_end"}, "loop");
    EXPECT_EQ(out.at(0), QString("loop"));
    EXPECT_TRUE(out.contains("loop2"));
    EXPECT_TRUE(out.contains("loop_end"));
}

TEST(CompletionScorer, KindsComeFromTheSymbolsShape)
{
    EXPECT_EQ(kindOf(".word"), Kind::Directive);
    EXPECT_EQ(kindOf(".globl"), Kind::Directive);
    EXPECT_EQ(kindOf("x10"), Kind::Register);
    EXPECT_EQ(kindOf("sp"), Kind::Register);
    EXPECT_EQ(kindOf("ra"), Kind::Register);
    EXPECT_EQ(kindOf("addi"), Kind::Instruction);
    EXPECT_EQ(kindOf("loop"), Kind::Instruction)
        << "a bare name is an instruction until the editor says it is a label";
}

// ═══════════════════════ the word under the caret ════════════════════════

TEST(CompletionWord, ThePrefixIsTheTextBeforeTheCaretOnly)
{
    // THE BUG: with the caret at the START of "sub", the prefix is empty.
    const QString text = "sub x0,x0,x0";
    const WordRange atStart = wordAt(text, 0);
    EXPECT_EQ(atStart.start, 0);
    EXPECT_EQ(atStart.caret, 0);
    EXPECT_EQ(atStart.end, 3) << "the word still ends at 3";
    EXPECT_EQ(text.mid(atStart.start, atStart.caret - atStart.start), QString(""))
        << "nothing is typed, so nothing is the prefix";

    // In the middle: prefix is what is before the caret, not the whole word.
    const WordRange middle = wordAt(text, 2);
    EXPECT_EQ(text.mid(middle.start, middle.caret - middle.start), QString("su"));
    EXPECT_EQ(middle.end, 3) << "the tail is still known, for replace mode";
}

TEST(CompletionWord, DotsAndDollarsAreInsideAWord)
{
    const QString text = "fadd.s f1, f2";
    const WordRange w = wordAt(text, 6); // after "fadd.s"
    EXPECT_EQ(w.start, 0);
    EXPECT_EQ(w.end, 6);
    EXPECT_EQ(text.mid(w.start, w.caret - w.start), QString("fadd.s"));
}

TEST(CompletionWord, OutsideAWordTheRangeIsEmpty)
{
    const QString text = "add  x1";
    const WordRange w = wordAt(text, 4); // the second space
    EXPECT_EQ(w.start, 4);
    EXPECT_EQ(w.end, 4);
    EXPECT_EQ(w.caret, 4);
}
