/**
 * @file lexer_directive_column_test.cpp
 * @brief Regression test for the column desync on directive lines.
 *
 * Lexer::directive() consumed the leading '.' by advancing pos_ without
 * advancing column_number_, so the counter ran one behind for the rest of that
 * line: the directive's own column span was one short and every token after it
 * reported a column one too small. Lexer::stringLiteral() then read its start
 * column after consuming the opening quote, which only looked correct because
 * the directive desync cancelled it out.
 *
 * All expected columns below were counted off the source text by hand.
 */
#include "assembler/lexer.h"
#include "assembler/tokens.h"

#include <gtest/gtest.h>

#include <sstream>
#include <vector>

using namespace Kites;

namespace
{
std::vector<Token> lexString(const std::string &source)
{
    std::istringstream stream(source);
    Lexer lexer(stream);
    return lexer.getTokenList();
}

Token tokenWithValue(const std::vector<Token> &tokens, unsigned line, const std::string &value)
{
    for (const Token &t : tokens)
        if (t.line_number == line && t.value == value)
            return t;
    return {};
}
} // namespace

TEST(LexerDirectiveColumn, DirectiveStartsAtTheDot)
{
    //    "    .data"
    //     1234^ the dot is column 5
    const auto tokens = lexString("    .data\n");
    EXPECT_EQ(tokenWithValue(tokens, 1, "data").column_number, 5u);
}

TEST(LexerDirectiveColumn, TokensAfterADirectiveKeepTheirTrueColumn)
{
    //    "    .word 1, 2, 3"
    //     1234 56789 0 1 ...
    //     '.word' occupies columns 5-9, space 10, '1' 11, ',' 12, '2' 14, '3' 17
    const auto tokens = lexString("    .word 1, 2, 3\n");
    EXPECT_EQ(tokenWithValue(tokens, 1, "word").column_number, 5u);
    EXPECT_EQ(tokenWithValue(tokens, 1, "1").column_number, 11u);
    EXPECT_EQ(tokenWithValue(tokens, 1, "2").column_number, 14u);
    EXPECT_EQ(tokenWithValue(tokens, 1, "3").column_number, 17u);
}

TEST(LexerDirectiveColumn, StringLiteralStartsAtItsOpeningQuote)
{
    //    "    .string \"hello world\""
    //     '.string' occupies 5-11, space 12, opening quote 13
    const auto tokens = lexString("    .string \"hello world\"\n");
    EXPECT_EQ(tokenWithValue(tokens, 1, "string").column_number, 5u);
    EXPECT_EQ(tokenWithValue(tokens, 1, "hello world").column_number, 13u);
}

TEST(LexerDirectiveColumn, ADirectiveDoesNotShiftTheFollowingLine)
{
    //    line 2 is unaffected by line 1's directive: 'addi' at column 5
    const auto tokens = lexString("    .text\n    addi t0, t1, 5000\n");
    EXPECT_EQ(tokenWithValue(tokens, 2, "addi").column_number, 5u);
    EXPECT_EQ(tokenWithValue(tokens, 2, "5000").column_number, 18u);
}
