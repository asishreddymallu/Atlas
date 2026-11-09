#pragma once
#include <string>
#include <string_view>

namespace Kites
{
/**
 * @file diagnostic_severity.h
 * @brief Severity and category shared by the assembler's ParseError and the
 *        editor's Diagnostic.
 *
 * Deliberately free of both Qt and assembler headers so either side can
 * include it without dragging the other in.
 *
 * ---------------------------------------------------------------------------
 * COLUMN CONVENTION - the single definition for the whole project
 * ---------------------------------------------------------------------------
 * Columns are **1-based**: the first character of a line is column 1. This
 * matches the lexer, which sets Token::column_number to 1 at the start of each
 * line (see lexer.cpp).
 *
 * A span is **[startColumn, endColumn)** - the end column is **exclusive**, so
 * a token at columns 5..7 inclusive is reported as start=5, end=8 and its
 * length is end - start.
 *
 * **0 means unknown** for either end. A diagnostic with startColumn == 0 has no
 * usable span and consumers fall back to highlighting the whole line.
 *
 * These stay 1-based all the way through the model and the JSON. Conversion to
 * QTextCursor positions (which are 0-based offsets within a block) happens
 * only at the UI boundary.
 * ---------------------------------------------------------------------------
 */

enum class DiagnosticSeverity
{
    Error,
    Warning,
    Information,
    Hint
};

enum class DiagnosticCategory
{
    Syntax,    ///< malformed source: bad mnemonic, bad register, missing operand
    Assembler, ///< well-formed but unassemblable: out-of-range immediate, unknown label
    Runtime    ///< raised while executing, not while assembling
};

constexpr std::string_view toString(DiagnosticSeverity severity)
{
    switch (severity)
    {
    case DiagnosticSeverity::Error:       return "error";
    case DiagnosticSeverity::Warning:     return "warning";
    case DiagnosticSeverity::Information: return "information";
    case DiagnosticSeverity::Hint:        return "hint";
    }
    return "error";
}

constexpr std::string_view toString(DiagnosticCategory category)
{
    switch (category)
    {
    case DiagnosticCategory::Syntax:    return "Syntax";
    case DiagnosticCategory::Assembler: return "Assembler";
    case DiagnosticCategory::Runtime:   return "Runtime";
    }
    return "Syntax";
}

/// Parses a severity name written by toString(). Unknown input yields `fallback`,
/// so an errors file from an older build still loads.
constexpr DiagnosticSeverity severityFromString(std::string_view text,
                                                DiagnosticSeverity fallback)
{
    if (text == "error")       return DiagnosticSeverity::Error;
    if (text == "warning")     return DiagnosticSeverity::Warning;
    if (text == "information") return DiagnosticSeverity::Information;
    if (text == "hint")        return DiagnosticSeverity::Hint;
    return fallback;
}

/// Parses a category name written by toString(). Unknown input yields `fallback`.
constexpr DiagnosticCategory categoryFromString(std::string_view text,
                                                DiagnosticCategory fallback)
{
    if (text == "Syntax")    return DiagnosticCategory::Syntax;
    if (text == "Assembler") return DiagnosticCategory::Assembler;
    if (text == "Runtime")   return DiagnosticCategory::Runtime;
    return fallback;
}
} // namespace Kites
