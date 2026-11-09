/**
 * @file lexer_token_span_test.cpp
 * @brief Covers Token::end_column, added in WP-04 stage 2a.
 *
 * end_column is taken from the lexer's column counter after the token has been
 * consumed, so it is the true source span even where Token::value is not the
 * verbatim source text - a hex literal is normalised ("0x1F" -> "31") and a
 * string literal's value drops its quotes, but both still report the span they
 * actually occupy.
 *
 * Convention (common/diagnostic_severity.h): 1-based, [column, end_column),
 * end exclusive, 0 = unknown.
 */
#include "assembler/lexer.h"
#include "assembler/tokens.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace Kites;

namespace
{
/// Lexes `source` verbatim (no trailing newline added) and returns its tokens.
std::vector<Token> lexString(const std::string &source)
{
    std::istringstream stream(source);
    Lexer lexer(stream);
    return lexer.getTokenList();
}

/// First token on `line` whose value matches, so tests do not depend on how
/// many tokens precede it.
Token tokenWithValue(const std::vector<Token> &tokens, unsigned line, const std::string &value)
{
    for (const Token &t : tokens)
        if (t.line_number == line && t.value == value)
            return t;
    return {};
}
} // namespace

// --------------------------------------------------------------------------
// hand-derived spans - the expected numbers below were counted off the source
// text by hand, not taken from what the lexer printed
// --------------------------------------------------------------------------

TEST(LexerTokenSpan, HexLiteralSpansItsSourceTextNotItsNormalisedValue)
{
    //    "    addi t0, t1, 0x1F"
    //     1234 5678 9 ...     ^ 0x1F starts at column 18, 4 characters -> end 22
    const auto tokens = lexString("    addi t0, t1, 0x1F\n");
    const Token imm = tokenWithValue(tokens, 1, "31"); // value is normalised
    EXPECT_EQ(imm.column_number, 18u);
    EXPECT_EQ(imm.end_column, 22u);
    EXPECT_NE(imm.end_column, imm.column_number + imm.value.length()); // the old rule was wrong
}

TEST(LexerTokenSpan, NegativeHexLiteralSpan)
{
    //    "    addi t0, t1, -0x1F"  -> -0x1F at column 18, 5 characters -> end 23
    const auto tokens = lexString("    addi t0, t1, -0x1F\n");
    const Token imm = tokenWithValue(tokens, 1, "-31");
    EXPECT_EQ(imm.column_number, 18u);
    EXPECT_EQ(imm.end_column, 23u);
}

TEST(LexerTokenSpan, LeadingZeroDecimalSpan)
{
    //    "    addi t0, t1, 007"   -> 007 at column 18, 3 characters -> end 21
    const auto tokens = lexString("    addi t0, t1, 007\n");
    const Token imm = tokenWithValue(tokens, 1, "7");
    EXPECT_EQ(imm.column_number, 18u);
    EXPECT_EQ(imm.end_column, 21u);
}

TEST(LexerTokenSpan, DirectiveSpanIncludesTheLeadingDot)
{
    //    "    .data"  -> '.' at column 5, ".data" is 5 characters -> end 10
    const auto tokens = lexString("    .data\n");
    const Token directive = tokenWithValue(tokens, 1, "data");
    EXPECT_EQ(directive.column_number, 5u);
    EXPECT_EQ(directive.end_column, 10u);
}

TEST(LexerTokenSpan, StringLiteralSpanIncludesBothQuotes)
{
    //    "    .string \"hello world\""
    //     opening quote at column 13, literal is 13 characters -> end 26
    const auto tokens = lexString("    .string \"hello world\"\n");
    const Token literal = tokenWithValue(tokens, 1, "hello world");
    EXPECT_EQ(literal.column_number, 13u);
    EXPECT_EQ(literal.end_column, 26u);
}

TEST(LexerTokenSpan, StringLiteralContainingAnEscapeSequence)
{
    //    "    .string \"a\\nb\""
    //     the lexer does not interpret escapes, so the backslash and the n are
    //     two ordinary characters: opening quote at column 13, "a\nb" is 6
    //     source characters -> end 19
    const auto tokens = lexString("    .string \"a\\nb\"\n");
    const Token literal = tokenWithValue(tokens, 1, "a\\nb");
    EXPECT_EQ(literal.column_number, 13u);
    EXPECT_EQ(literal.end_column, 19u);
    EXPECT_EQ(literal.end_column - literal.column_number, 6u);
}

TEST(LexerTokenSpan, TokenAtEndOfLineWithoutATrailingNewline)
{
    //    "    addi t0, t1, 5000"  with no '\n' at all
    //     5000 at column 18, 4 characters -> end 22
    const auto tokens = lexString("    addi t0, t1, 5000");
    const Token imm = tokenWithValue(tokens, 1, "5000");
    EXPECT_EQ(imm.column_number, 18u);
    EXPECT_EQ(imm.end_column, 22u);
}

TEST(LexerTokenSpan, TabIndentedLineCountsEachTabAsOneColumn)
{
    //    "\taddi t0, t1, 5000"
    //     the tab is column 1, so addi starts at column 2 and 5000 at column 15
    const auto tokens = lexString("\taddi t0, t1, 5000\n");
    const Token opcode = tokenWithValue(tokens, 1, "addi");
    EXPECT_EQ(opcode.column_number, 2u);
    EXPECT_EQ(opcode.end_column, 6u);

    const Token imm = tokenWithValue(tokens, 1, "5000");
    EXPECT_EQ(imm.column_number, 15u);
    EXPECT_EQ(imm.end_column, 19u);
}

// --------------------------------------------------------------------------
// corpus-wide invariant
// --------------------------------------------------------------------------

TEST(LexerTokenSpan, EveryTokenInEveryExampleHasAPositiveSpan)
{
    // tests run with the working directory set to test/ (see test/CMakeLists.txt)
    const std::filesystem::path examples = "../examples";
    ASSERT_TRUE(std::filesystem::exists(examples)) << "examples/ not found";

    std::size_t files = 0;
    std::size_t tokens = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(examples))
    {
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        if (ext != ".s" && ext != ".asm")
            continue;

        ++files;
        Lexer lexer(entry.path().string());
        for (const Token &t : lexer.getTokenList())
        {
            if (t.type == TokenType::EOF_)
                continue; // the end marker occupies no source text
            ++tokens;
            EXPECT_GT(t.end_column, t.column_number)
                << entry.path().string() << " line " << t.line_number << " value \"" << t.value
                << "\" has end_column " << t.end_column << " <= column " << t.column_number;
        }
    }
    EXPECT_EQ(files, 44u) << "the examples corpus changed size";
    EXPECT_GT(tokens, 5000u);
}
