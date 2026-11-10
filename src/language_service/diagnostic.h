#pragma once
#include "common/diagnostic_severity.h"
#include <QByteArray>
#include <QString>
#include <QVector>

namespace Kites
{

// A single reported problem, normalized from the assembler's various error
// struct types (see src/assembler/errors.h) into one shape the editor can
// render as a squiggle without knowing about assembler internals.
//
// Columns follow the project-wide convention documented in
// common/diagnostic_severity.h: 1-based, [startColumn, endColumn) with an
// exclusive end, and 0 meaning unknown. They stay 1-based here; conversion to
// QTextCursor offsets happens only at the UI boundary.
struct Diagnostic
{
    /// 1-based line in the text that was actually assembled. When custom
    /// pseudo-instructions are defined that is the *expanded* text, not what
    /// the user typed. 0 = unknown.
    int line        = 0;
    /// 1-based line in the user's original source, mapped back from `line`
    /// through customPseudoManager's expandedLineToOriginalLine. Filled in by
    /// EditorTab, which owns the mapping. 0 = not mapped.
    int originalLine = 0;
    int startColumn = 0; ///< 1-based first column of the token. 0 = unknown.
    int endColumn   = 0; ///< 1-based, exclusive. 0 = unknown.
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    DiagnosticCategory category = DiagnosticCategory::Syntax;
    QString message;
};

/**
 * @brief Parses the JSON written by DumpErrors() into Diagnostics.
 *
 * Extracted from EditorTab::setErrorLinesFromFile so it can be tested without
 * standing up the UI. Behaviour for malformed input is deliberately identical
 * to the code it replaced: anything that is not a JSON object yields an empty
 * vector rather than throwing, and entries are taken from the "errors" array.
 *
 * Reads every field added after the original two (line, message) with a
 * default, so an errors file written by an older build still loads.
 *
 * @param json Raw file contents.
 * @return One Diagnostic per entry; empty if the document is absent or invalid.
 */
QVector<Diagnostic> parseDiagnosticsJson(const QByteArray &json);

} // namespace Kites
