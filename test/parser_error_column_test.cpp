/**
 * @file parser_error_column_test.cpp
 * @brief ParseError spans and categories, added in WP-04 stage 2b.
 *
 * Every expected column below was counted off the source text by hand before
 * the test was run. Convention (common/diagnostic_severity.h): 1-based,
 * [column, end_column), end exclusive, 0 = unknown.
 *
 * Each site passes the SAME token expression the rich error beside it already
 * used, and takes end_column from Token::end_column rather than from
 * value.length() - the two differ whenever the lexer normalises a value, e.g.
 * 0xFFFF lexes to "65535".
 */
#include "assembler/lexer.h"
#include "assembler/parser.h"
#include "common/diagnostic_severity.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Kites;

namespace
{
/// Writes `source` to a temp file, parses it, and returns the recorded errors.
std::vector<ParseError> parseErrors(const std::string &stem, const std::string &source)
{
    const auto path = std::filesystem::temp_directory_path() / ("kites_pe_" + stem + ".s");
    { std::ofstream out(path); out << source; }

    Lexer lexer(path.string());
    auto tokens = lexer.getTokenList();
    Parser parser(path.string(), tokens);
    try { parser.parse(); } catch (const std::exception &) { /* parse throws on error */ }
    auto errors = parser.getErrors();
    std::filesystem::remove(path);
    return errors;
}
} // namespace

// --------------------------------------------------------------------------
// token-level spans
// --------------------------------------------------------------------------

TEST(ParserErrorColumn, BadMnemonicSpansTheUnexpectedToken)
{
    //    line 2: "    xyzzy t0, t1, t2"
    //             1234 ^ xyzzy occupies columns 5-9 -> span 5..10
    const auto errors = parseErrors("bad_mnemonic", "main:\n    xyzzy t0, t1, t2\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 5u);
    EXPECT_EQ(errors[0].end_column, 10u);
    EXPECT_EQ(errors[0].category, DiagnosticCategory::Syntax);
    EXPECT_EQ(errors[0].severity, DiagnosticSeverity::Error);
}

// Named for what it actually does: parser.cpp:497 reports the instruction as a
// whole, so currentToken() is the mnemonic and the span covers "addi", not the
// bad operand. This is a known limit, recorded in docs/audit/known_issues.md.
TEST(ParserErrorColumn, BadRegisterSpansTheMnemonicNotTheOffendingOperand)
{
    //    line 2: "    addi x99, x0, 1"   addi occupies columns 5-8 -> span 5..9
    const auto errors = parseErrors("bad_register", "main:\n    addi x99, x0, 1\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 5u);
    EXPECT_EQ(errors[0].end_column, 9u);
    EXPECT_EQ(errors[0].category, DiagnosticCategory::Syntax);
}

// Same site, same limit: the span is the mnemonic, not the position where the
// missing operand should have been.
TEST(ParserErrorColumn, MissingOperandSpansTheMnemonicNotTheGap)
{
    //    line 2: "    addi t0, t1"       addi occupies columns 5-8 -> span 5..9
    //    the error is reported on line 2, not on the following line or at EOF
    const auto errors = parseErrors("missing_operand", "main:\n    addi t0, t1\n    addi t2, t3, 4\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 5u);
    EXPECT_EQ(errors[0].end_column, 9u);
}

TEST(ParserErrorColumn, DecimalImmediateOutOfRangeSpansTheImmediate)
{
    //    line 2: "    addi t0, t1, 5000"
    //    1-4 sp, addi 5-8, sp 9, t0 10-11, ',' 12, sp 13, t1 14-15, ',' 16,
    //    sp 17, 5000 18-21 -> span 18..22
    const auto errors = parseErrors("imm_dec", "main:\n    addi t0, t1, 5000\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 18u);
    EXPECT_EQ(errors[0].end_column, 22u);
    EXPECT_EQ(errors[0].category, DiagnosticCategory::Assembler);
}

TEST(ParserErrorColumn, HexImmediateOutOfRangeSpansTheWholeLiteral)
{
    //    line 2: "    addi t0, t1, 0xFFFF"   0xFFFF occupies 18-23 -> span 18..24
    //    this is the case value.length() gets wrong: the lexer normalises
    //    "0xFFFF" to "65535", so a length-based end would be 23, not 24
    const auto errors = parseErrors("imm_hex", "main:\n    addi t0, t1, 0xFFFF\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 18u);
    EXPECT_EQ(errors[0].end_column, 24u);
    EXPECT_EQ(errors[0].end_column - errors[0].column, 6u); // "0xFFFF" is 6 chars
}

TEST(ParserErrorColumn, TokenBasedLabelErrorSpansTheLabel)
{
    //    i_m_formats.cpp:432 - a load whose label is not defined in .data
    //    line 3: "    lw t0, missing"
    //    1-4 sp, lw 5-6, sp 7, t0 8-9, ',' 10, sp 11, missing 12-18 -> span 12..19
    const auto errors = parseErrors("label_ref", "    .text\nmain:\n    lw t0, missing\n");
    ASSERT_GE(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 3u);
    EXPECT_EQ(errors[0].column, 12u);
    EXPECT_EQ(errors[0].end_column, 19u);
    EXPECT_EQ(errors[0].category, DiagnosticCategory::Assembler);
}

// --------------------------------------------------------------------------
// the documented column-0 case
// --------------------------------------------------------------------------

TEST(ParserErrorColumn, UnknownBranchLabelHasNoColumnButIsCategorisedAssembler)
{
    //    "jal ra, nowhere" is resolved in the second pass over ICUnit blocks,
    //    where no Token is in scope (parser.cpp:767). It reports a line but no
    //    column, and consumers fall back to the whole line.
    //
    //    The category is correct even though the column is not: the six
    //    second-pass sites pass DiagnosticCategory::Assembler explicitly with
    //    an explicit 0 column. See docs/audit/known_issues.md.
    const auto errors = parseErrors("unknown_label", "main:\n    jal ra, nowhere\n");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].line, 2u);
    EXPECT_EQ(errors[0].column, 0u);
    EXPECT_EQ(errors[0].end_column, 0u);
    EXPECT_EQ(errors[0].category, DiagnosticCategory::Assembler);
}

// --------------------------------------------------------------------------
// non-ASCII: the lexer counts bytes, QTextCursor counts UTF-16 units
// --------------------------------------------------------------------------

TEST(LexerTokenSpanNonAscii, ColumnsCountBytesNotCharacters)
{
    //    "    .string \"caf<U+00E9>\", \"x\""  with the e-acute encoded as the two
    //    UTF-8 bytes 0xC3 0xA9.
    //
    //    By byte:  1-4 sp, '.' 5, string 6-11, sp 12, '"' 13, c 14, a 15,
    //              f 16, e-acute 17-18, '"' 19, ',' 20, sp 21, '"' 22, x 23, '"' 24
    //
    //    So the second literal starts at BYTE column 22. A QTextCursor counts
    //    UTF-16 units, where the e-acute is a single unit, so the same position
    //    is UTF-16 column 21 - one less, matching the one extra byte before it.
    //    Converting byte columns to cursor offsets is the UI's job (stage 5).
    const std::string source = "    .data\nm:\n    .string \"caf\xc3\xa9\", \"x\"\n";
    const auto path = std::filesystem::temp_directory_path() / "kites_pe_utf8.s";
    { std::ofstream out(path); out << source; }
    Lexer lexer(path.string());
    const auto tokens = lexer.getTokenList();
    std::filesystem::remove(path);

    const Token *first = nullptr;
    const Token *second = nullptr;
    for (const Token &t : tokens)
    {
        if (t.line_number != 3 || t.type != TokenType::STRING) continue;
        (first ? second : first) = &t;
    }
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    EXPECT_EQ(first->column_number, 13u);
    EXPECT_EQ(first->end_column, 20u);   // 7 bytes: quote + c,a,f + 2 for e-acute + quote
    EXPECT_EQ(second->column_number, 22u);
    EXPECT_EQ(second->end_column, 25u);

    // the line is one byte longer than it is UTF-16 units
    const std::string line = "    .string \"caf\xc3\xa9\", \"x\"";
    EXPECT_EQ(line.size(), 24u);                   // bytes
    EXPECT_EQ(second->column_number - 1u, 21u);    // UTF-16 column would be 21
}
