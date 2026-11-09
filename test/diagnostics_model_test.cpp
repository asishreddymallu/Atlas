/**
 * @file diagnostics_model_test.cpp
 * @brief Covers the diagnostic model added in WP-04 stage 1: ParseError's new
 *        fields, the severity/category enums, the DumpErrors JSON round trip,
 *        and parseDiagnosticsJson()'s behaviour on malformed input.
 *
 * Column convention under test (see common/diagnostic_severity.h): columns are
 * 1-based, a span is [startColumn, endColumn) with an exclusive end, and 0
 * means unknown.
 */
#include "assembler/parser.h"
#include "common/diagnostic_severity.h"
#include "language_service/diagnostic.h"
#include "utils/utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Kites;

namespace
{
std::filesystem::path tempErrorsFile(const std::string &stem)
{
    return std::filesystem::temp_directory_path() / ("kites_" + stem + "_errors.json");
}

QByteArray readAll(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    return QByteArray(text.data(), static_cast<qsizetype>(text.size()));
}
} // namespace

// --------------------------------------------------------------------------
// ParseError
// --------------------------------------------------------------------------

TEST(DiagnosticModel, ParseErrorLegacyConstructorLeavesSpanUnknown)
{
    // the original two-argument constructor must keep working unchanged
    const ParseError error(7, "Immediate value out of range");

    EXPECT_EQ(error.line, 7u);
    EXPECT_EQ(error.message, "Immediate value out of range");
    // column used to be left uninitialised here; it must now read 0 = unknown
    EXPECT_EQ(error.column, 0u);
    EXPECT_EQ(error.end_column, 0u);
    EXPECT_EQ(error.severity, DiagnosticSeverity::Error);
    EXPECT_EQ(error.category, DiagnosticCategory::Syntax);
}

TEST(DiagnosticModel, ParseErrorSpanConstructorSetsEveryField)
{
    const ParseError error(12, 5, 8, "Invalid register", DiagnosticSeverity::Warning,
                           DiagnosticCategory::Assembler);

    EXPECT_EQ(error.line, 12u);
    EXPECT_EQ(error.column, 5u);      // 1-based, inclusive
    EXPECT_EQ(error.end_column, 8u);  // 1-based, exclusive -> token is 3 chars
    EXPECT_EQ(error.end_column - error.column, 3u);
    EXPECT_EQ(error.severity, DiagnosticSeverity::Warning);
    EXPECT_EQ(error.category, DiagnosticCategory::Assembler);
    EXPECT_EQ(error.message, "Invalid register");
}

// --------------------------------------------------------------------------
// enums
// --------------------------------------------------------------------------

TEST(DiagnosticModel, SeverityAndCategoryRoundTripThroughStrings)
{
    for (const DiagnosticSeverity severity :
         {DiagnosticSeverity::Error, DiagnosticSeverity::Warning,
          DiagnosticSeverity::Information, DiagnosticSeverity::Hint})
    {
        EXPECT_EQ(severityFromString(toString(severity), DiagnosticSeverity::Hint), severity);
    }

    for (const DiagnosticCategory category :
         {DiagnosticCategory::Syntax, DiagnosticCategory::Assembler,
          DiagnosticCategory::Runtime})
    {
        EXPECT_EQ(categoryFromString(toString(category), DiagnosticCategory::Runtime), category);
    }
}

TEST(DiagnosticModel, UnknownEnumTextFallsBackSoOlderFilesStillLoad)
{
    EXPECT_EQ(severityFromString("fatal", DiagnosticSeverity::Error), DiagnosticSeverity::Error);
    EXPECT_EQ(severityFromString("", DiagnosticSeverity::Warning), DiagnosticSeverity::Warning);
    EXPECT_EQ(categoryFromString("Linker", DiagnosticCategory::Syntax),
              DiagnosticCategory::Syntax);
}

// --------------------------------------------------------------------------
// DumpErrors -> parseDiagnosticsJson round trip
// --------------------------------------------------------------------------

TEST(DiagnosticModel, JsonRoundTripPreservesEveryField)
{
    const std::vector<ParseError> errors{
        ParseError(4, 9, 14, "Invalid register", DiagnosticSeverity::Error,
                   DiagnosticCategory::Syntax),
        ParseError(11, 3, 7, "Immediate value out of range", DiagnosticSeverity::Warning,
                   DiagnosticCategory::Assembler),
    };

    const auto path = tempErrorsFile("roundtrip");
    DumpErrors(path, errors);
    const QVector<Diagnostic> parsed = parseDiagnosticsJson(readAll(path));
    std::filesystem::remove(path);

    ASSERT_EQ(parsed.size(), 2);

    EXPECT_EQ(parsed[0].line, 4);
    EXPECT_EQ(parsed[0].startColumn, 9);
    EXPECT_EQ(parsed[0].endColumn, 14);
    EXPECT_EQ(parsed[0].severity, DiagnosticSeverity::Error);
    EXPECT_EQ(parsed[0].category, DiagnosticCategory::Syntax);
    EXPECT_EQ(parsed[0].message, QString("Invalid register"));

    EXPECT_EQ(parsed[1].line, 11);
    EXPECT_EQ(parsed[1].startColumn, 3);
    EXPECT_EQ(parsed[1].endColumn, 7);
    EXPECT_EQ(parsed[1].severity, DiagnosticSeverity::Warning);
    EXPECT_EQ(parsed[1].category, DiagnosticCategory::Assembler);
}

TEST(DiagnosticModel, JsonRoundTripSurvivesQuotesBackslashesAndControlCharacters)
{
    // before escaping was added this produced invalid JSON and the editor
    // silently showed no diagnostics at all
    const std::string nasty = R"(unexpected "x0\y" after)" "\n\ttab";
    const std::vector<ParseError> errors{ParseError(1, 1, 2, nasty)};

    const auto path = tempErrorsFile("escaping");
    DumpErrors(path, errors);
    const QVector<Diagnostic> parsed = parseDiagnosticsJson(readAll(path));
    std::filesystem::remove(path);

    ASSERT_EQ(parsed.size(), 1);
    EXPECT_EQ(parsed[0].message, QString::fromStdString(nasty));
}

// --------------------------------------------------------------------------
// parseDiagnosticsJson: unchanged behaviour on input the old code also saw
// --------------------------------------------------------------------------

TEST(DiagnosticModel, MissingFileContentsYieldNoDiagnostics)
{
    // EditorTab returns early when the file cannot be opened, so the parser
    // never sees anything; the equivalent here is a null byte array
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray()).isEmpty());
}

TEST(DiagnosticModel, EmptyOrMalformedFileYieldsNoDiagnostics)
{
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray("")).isEmpty());
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray("   \n  ")).isEmpty());
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray("not json at all")).isEmpty());
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray("[1, 2, 3]")).isEmpty());   // array, not object
    EXPECT_TRUE(parseDiagnosticsJson(QByteArray(R"({"errorCode": 1})")).isEmpty()); // no "errors"
}

TEST(DiagnosticModel, BadLineNumbersSurviveAsZeroForTheConsumerToSkip)
{
    // QJsonValue::toInt() yields 0 for anything non-numeric, exactly as the
    // code this replaced did; applyDiagnostics() then skips line <= 0
    const QVector<Diagnostic> parsed = parseDiagnosticsJson(QByteArray(R"({
        "errorCode": 1,
        "errors": [
            {"line": "oops", "message": "non-numeric line"},
            {"message": "no line key at all"},
            {"line": -5, "message": "negative line"}
        ]
    })"));

    ASSERT_EQ(parsed.size(), 3);
    EXPECT_EQ(parsed[0].line, 0);
    EXPECT_EQ(parsed[1].line, 0);
    EXPECT_EQ(parsed[2].line, -5);
}

TEST(DiagnosticModel, OldStyleFileWithoutTheNewKeysStillLoads)
{
    // an errors file written before this work package
    const QVector<Diagnostic> parsed = parseDiagnosticsJson(QByteArray(R"({
        "errorCode": 1,
        "errors": [
            {"line": 4, "message": "Immediate value out of range"}
        ]
    })"));

    ASSERT_EQ(parsed.size(), 1);
    EXPECT_EQ(parsed[0].line, 4);
    EXPECT_EQ(parsed[0].message, QString("Immediate value out of range"));
    EXPECT_EQ(parsed[0].startColumn, 0); // unknown -> consumer falls back to whole line
    EXPECT_EQ(parsed[0].endColumn, 0);
    EXPECT_EQ(parsed[0].severity, DiagnosticSeverity::Error);
    EXPECT_EQ(parsed[0].category, DiagnosticCategory::Syntax);
}
