/**
 * @file label_index_test.cpp
 * @brief Where the labels are, as a table. No widget, no cursor.
 *
 * Every case is a GUARD on what F12, Shift+F12 and F2 will act on.
 */
#include "ui/editor_tab/label_index.h"

#include <gtest/gtest.h>

using namespace Kites::LabelIndex;

namespace
{
QStringList names(const QList<Definition> &definitions)
{
    QStringList out;
    for (const Definition &d : definitions)
        out << d.name;
    return out;
}
QStringList names(const QList<Reference> &references)
{
    QStringList out;
    for (const Reference &r : references)
        out << r.name;
    return out;
}
} // namespace

TEST(LabelIndexDefinitions, ALabelIsANameFollowedByAColon)
{
    const Index index = findLabels("main:\n    addi x1, x0, 4\nloop:\n    j loop\n");
    EXPECT_EQ(names(index.definitions), QStringList({"main", "loop"}));
    EXPECT_EQ(index.definitions.at(0).line, 0);
    EXPECT_EQ(index.definitions.at(0).column, 0);
    EXPECT_EQ(index.definitions.at(0).offset, 0);
    EXPECT_EQ(index.definitions.at(1).line, 2);
}

TEST(LabelIndexDefinitions, LeadingWhitespaceDoesNotStopIt)
{
    const Index index = findLabels("        indented:\n    nop\n");
    ASSERT_EQ(index.definitions.size(), 1);
    EXPECT_EQ(index.definitions.at(0).name, QString("indented"));
    EXPECT_EQ(index.definitions.at(0).column, 8);
}

TEST(LabelIndexDefinitions, ALabelMayBeFollowedByAnInstructionOnTheSameLine)
{
    const Index index = findLabels("loop: addi x1, x1, 1\n    j loop\n");
    EXPECT_EQ(names(index.definitions), QStringList({"loop"}));
    EXPECT_EQ(names(index.references), QStringList({"loop"}));
    EXPECT_EQ(index.references.at(0).line, 1);
}

TEST(LabelIndexDefinitions, TwoLabelsMayShareALine)
{
    const Index index = findLabels("first: second: nop\n");
    EXPECT_EQ(names(index.definitions), QStringList({"first", "second"}));
    EXPECT_EQ(index.definitions.at(0).column, 0);
    EXPECT_EQ(index.definitions.at(1).column, 7);
}

TEST(LabelIndexDefinitions, ADuplicateIsReportedTwiceAndNamed)
{
    const Index index = findLabels("dup:\n    nop\ndup:\n    nop\n");
    EXPECT_EQ(index.definitionsOf("dup").size(), 2);
    EXPECT_EQ(index.duplicates(), QStringList({"dup"}));
    // Navigation goes to the first.
    EXPECT_EQ(index.definitionsOf("dup").at(0).line, 0);
}

TEST(LabelIndexDefinitions, UnderscoresAndDigitsAreInsideANameButDotsAreNot)
{
    // The assembler rejects a label whose name contains '.', and '$' never
    // enters its identifier scan at all.
    const Index index = findLabels("loop_end2:\n    nop\n");
    EXPECT_EQ(names(index.definitions), QStringList({"loop_end2"}));

    const Index dotted = findLabels("has.dot:\n    nop\n");
    // "has" and "dot" are separate tokens; only the one before ':' defines.
    EXPECT_EQ(names(dotted.definitions), QStringList({"dot"}))
        << "a dotted name is not one label";
}

// ═══════════════════════ comments and strings ════════════════════════════

TEST(LabelIndexContext, ANameInACommentIsNotALabel)
{
    const Index index = findLabels("main:\n    nop     # loop: not a label\n");
    EXPECT_EQ(names(index.definitions), QStringList({"main"}));
}

TEST(LabelIndexContext, ANameInAStringIsNotALabel)
{
    const Index index = findLabels("msg:\n    .asciz \"inside: not a label\"\n");
    EXPECT_EQ(names(index.definitions), QStringList({"msg"}));
}

TEST(LabelIndexContext, AReferenceInACommentDoesNotCount)
{
    const Index index = findLabels("loop:\n    nop   # j loop\n");
    EXPECT_EQ(names(index.definitions), QStringList({"loop"}));
    EXPECT_TRUE(index.references.isEmpty()) << "a reference inside a comment was counted";
}

// ════════════════════════════ references ═════════════════════════════════

TEST(LabelIndexReferences, OnlyDefinedNamesCount)
{
    // "nowhere" is not defined, so it is not a reference - and neither are
    // the registers or the mnemonic.
    const Index index = findLabels("start:\n    beq x1, x2, nowhere\n    j start\n");
    EXPECT_EQ(names(index.references), QStringList({"start"}));
}

TEST(LabelIndexReferences, APrefixOfAnotherLabelDoesNotCollide)
{
    const Index index = findLabels(
        "loop:\nloop2:\nloop_end:\n    j loop\n    j loop2\n    j loop_end\n");
    EXPECT_EQ(names(index.definitions), QStringList({"loop", "loop2", "loop_end"}));
    QStringList referenced = names(index.references);
    referenced.sort();
    EXPECT_EQ(referenced, QStringList({"loop", "loop2", "loop_end"}));
    // Each reference names exactly one label.
    for (const Reference &r : index.references)
        EXPECT_TRUE(index.isDefined(r.name));
}

TEST(LabelIndexReferences, ALabelNamedLikeAMnemonicIsStillAReference)
{
    // The assembler's own rule: an identifier in operand position is a
    // label reference even when its text is an instruction.
    const Index index = findLabels("add:\n    nop\n    j add\n");
    EXPECT_EQ(names(index.definitions), QStringList({"add"}));
    EXPECT_EQ(names(index.references), QStringList({"add"}));
    EXPECT_EQ(index.references.at(0).line, 2);
}

TEST(LabelIndexReferences, AReferenceOnTheSameLineAsItsDefinition)
{
    const Index index = findLabels("spin: j spin\n");
    EXPECT_EQ(names(index.definitions), QStringList({"spin"}));
    ASSERT_EQ(index.references.size(), 1);
    EXPECT_EQ(index.references.at(0).line, 0);
    EXPECT_GT(index.references.at(0).column, index.definitions.at(0).column);
}

// ═══════════════════════════ edge documents ══════════════════════════════

TEST(LabelIndexEdges, AnEmptyDocumentHasNothing)
{
    const Index index = findLabels("");
    EXPECT_TRUE(index.definitions.isEmpty());
    EXPECT_TRUE(index.references.isEmpty());
}

TEST(LabelIndexEdges, TheLastLineWithNoTrailingNewline)
{
    const Index index = findLabels("main:\n    nop\nlast:");
    EXPECT_EQ(names(index.definitions), QStringList({"main", "last"}));
    EXPECT_EQ(index.definitions.at(1).line, 2);
}

TEST(LabelIndexEdges, CarriageReturnsDoNotBecomePartOfAName)
{
    // The editor can hold CRLF if a file brought it in.
    const Index index = findLabels("main:\r\n    j main\r\n");
    EXPECT_EQ(names(index.definitions), QStringList({"main"}))
        << "a '\\r' leaked into the name";
    EXPECT_EQ(names(index.references), QStringList({"main"}));
}

TEST(LabelIndexEdges, OffsetsPointAtTheNameItself)
{
    const QString text = "aaa:\n    j aaa\n";
    const Index   index = findLabels(text);
    ASSERT_EQ(index.definitions.size(), 1);
    ASSERT_EQ(index.references.size(), 1);
    EXPECT_EQ(text.mid(index.definitions.at(0).offset, 3), QString("aaa"));
    EXPECT_EQ(text.mid(index.references.at(0).offset, 3), QString("aaa"));
}

// ═══════════════════════════ nameAt ══════════════════════════════════════

TEST(LabelIndexNameAt, TheWholeTokenIsReturnedFromAnywhereInIt)
{
    const QString text = "    j loop_end\n";
    EXPECT_EQ(nameAt(text, 6), QString("loop_end"));  // at the start
    EXPECT_EQ(nameAt(text, 9), QString("loop_end"));  // in the middle
    EXPECT_EQ(nameAt(text, 14), QString("loop_end")); // at the end
    // Offset 5 sits just AFTER the 'j', so the token there is "j" - a caret
    // at the end of a token is on that token, which is what F12 wants.
    EXPECT_EQ(nameAt(text, 5), QString("j"));
    // Inside the leading whitespace, with no token on either side, there is
    // no name at all.
    EXPECT_EQ(nameAt(text, 2), QString());
}

// ══════════════ one definition of "this line defines a label" ════════════

/**
 * @brief The completion scanner and the navigation index must agree.
 *
 * They each carried their own copy of the rule until scanLineLabels() was
 * made to delegate to LabelIndex::definitionsOnLine(). Nothing in the
 * compiler stops them drifting apart again, so this asserts it over real
 * files and over the traps: every label one finds, the other finds, and in
 * the same place.
 *
 * The scanner side is reached through the same function the editor calls,
 * so this cannot pass by testing a copy.
 */
TEST(LabelIndexConsistency, ThePerLineRuleAgreesWithTheWholeDocumentScan)
{
    const QStringList documents = {
        // the traps, first
        "main:\n    nop     # loop: not a label\n",
        "msg:\n    .asciz \"inside: not a label\"\n",
        "add:\n    nop\n    j add\n",
        "loop:\nloop2:\nloop_end:\n    j loop\n",
        "first: second: nop\n",
        "start: addi x1, x0, 1\n",
        "        indented:\n    nop\n",
        "has.dot:\n    nop\n",
        "main:\r\n    j main\r\n",
        "main:\n    nop\nlast:",
        "",
        // something ordinary
        "main:\n    addi x1, x0, 4\nloop:\n    j loop\n    ret\n",
    };

    for (const QString &text : documents)
    {
        const Index whole = findLabels(text);

        // What the per-line rule says, walked line by line as the editor's
        // completion scanner walks it.
        QStringList perLine;
        for (const QString &line : text.split('\n'))
            perLine << definitionsOnLine(line);

        QStringList fromWhole;
        for (const Definition &d : whole.definitions)
            fromWhole << d.name;

        EXPECT_EQ(perLine, fromWhole)
            << "the two scanners disagree on:\n" << text.toStdString();
    }
}
