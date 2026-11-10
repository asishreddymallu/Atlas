/**
 * @file editor_diagnostic_mapping_test.cpp
 * @brief EditorTab mapping diagnostics from the expanded text back to the
 *        user's source, and building each pane's cursors from its own document.
 *
 * The assembler runs on the pseudo-expanded text, so every line and column it
 * reports is an expanded one. With custom pseudo-instructions defined those
 * line numbers drift from the user's source. The programs below are the ones
 * from the stage 3 investigation.
 */
#include "ui/editor_tab/editortab.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextEdit>

#include <filesystem>
#include <vector>

using namespace Kites;

namespace
{
/// Writes an errors file in the shape DumpErrors produces and feeds it in.
void feedDiagnostic(EditorTab &tab, int line, int column, int endColumn,
                    const char *message = "Immediate value out of range")
{
    const auto path = std::filesystem::temp_directory_path() / "kites_map_errors.json";
    QFile file(QString::fromStdString(path.string()));
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QString(R"({"errorCode":1,"errors":[{"line":%1,"column":%2,"endColumn":%3,)"
                       R"("severity":"error","category":"Assembler","message":"%4"}]})")
                   .arg(line).arg(column).arg(endColumn).arg(message).toUtf8());
    file.close();
    tab.setErrorLinesFromFile(path);
    std::filesystem::remove(path);
}

QPlainTextEdit *inputPane(EditorTab &tab)
{
    return tab.findChild<QPlainTextEdit *>("assemblyTextEdit");
}
QPlainTextEdit *expandedPane(EditorTab &tab)
{
    return tab.findChild<QPlainTextEdit *>("expandedTextEdit");
}

/// 1-based line a selection sits on.
int selectionLine(const QTextEdit::ExtraSelection &selection)
{
    return selection.cursor.blockNumber() + 1;
}

/// KitesEditor::extraSelections() is the merged list of the current-line
/// highlight and the diagnostic squiggles. Keep only the squiggles, which are
/// the ones carrying a wave underline.
QList<QTextEdit::ExtraSelection> squiggles(QPlainTextEdit *pane)
{
    QList<QTextEdit::ExtraSelection> result;
    for (const QTextEdit::ExtraSelection &selection : pane->extraSelections())
    {
        if (selection.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            result.append(selection);
    }
    return result;
}
} // namespace

// --------------------------------------------------------------------------
// the bigadd program: a generated line, shifted away from its source line
// --------------------------------------------------------------------------

TEST(EditorDiagnosticMapping, InputPaneSquiggleLandsOnTheSourceLineNotTheExpandedOne)
{
    //  ORIGINAL                     EXPANDED
    //  1 | main:                    1 | main:
    //  2 |     push t0              2 | addi sp, sp, -8
    //  3 |     bigadd t3            3 | sd t0, 0(sp)
    //  4 |     ret                  4 | addi t3, t1, 5000   <- error reported here
    //                               5 |     ret
    //  expanded line 4 maps back to original line 3
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    push t0\n    bigadd t3\n    ret\n");
    expandedPane(tab)->setPlainText(
        "main:\naddi sp, sp, -8\nsd t0, 0(sp)\naddi t3, t1, 5000\n    ret\n");
    tab.setExpansionMapping({1, 2, 2, 3, 4, 5});

    feedDiagnostic(tab, /*line=*/4, /*column=*/14, /*endColumn=*/18);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    EXPECT_EQ(selectionLine(input[0]), 3) << "must point at 'bigadd t3', not 'ret'";
}

TEST(EditorDiagnosticMapping, GeneratedLineFallsBackToTheWholeOriginalLine)
{
    // expanded line 4 is "addi t3, t1, 5000", which the user never wrote, so
    // its columns mean nothing in the source: squiggle the whole line instead
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    push t0\n    bigadd t3\n    ret\n");
    expandedPane(tab)->setPlainText(
        "main:\naddi sp, sp, -8\nsd t0, 0(sp)\naddi t3, t1, 5000\n    ret\n");
    tab.setExpansionMapping({1, 2, 2, 3, 4, 5});

    feedDiagnostic(tab, 4, 14, 18);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    const QTextCursor cursor = input[0].cursor;
    // whole line: "    bigadd t3" is 13 characters
    EXPECT_EQ(cursor.selectionStart() - cursor.block().position(), 0);
    EXPECT_EQ(cursor.selectionEnd() - cursor.block().position(), 13);
}

TEST(EditorDiagnosticMapping, EachPaneGetsCursorsFromItsOwnDocument)
{
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    push t0\n    bigadd t3\n    ret\n");
    expandedPane(tab)->setPlainText(
        "main:\naddi sp, sp, -8\nsd t0, 0(sp)\naddi t3, t1, 5000\n    ret\n");
    tab.setExpansionMapping({1, 2, 2, 3, 4, 5});

    feedDiagnostic(tab, 4, 14, 18);

    const auto input    = squiggles(inputPane(tab));
    const auto expanded = squiggles(expandedPane(tab));
    ASSERT_EQ(input.size(), 1);
    ASSERT_EQ(expanded.size(), 1);

    EXPECT_EQ(input[0].cursor.document(), inputPane(tab)->document());
    EXPECT_EQ(expanded[0].cursor.document(), expandedPane(tab)->document());

    // the expanded pane keeps the exact expanded line and span
    EXPECT_EQ(selectionLine(expanded[0]), 4);
    const QTextCursor cursor = expanded[0].cursor;
    EXPECT_EQ(cursor.selectionStart() - cursor.block().position(), 13); // column 14, 1-based
    EXPECT_EQ(cursor.selectionEnd() - cursor.block().position(), 17);   // column 18, exclusive
}

// --------------------------------------------------------------------------
// the common case: no custom pseudo-instructions
// --------------------------------------------------------------------------

TEST(EditorDiagnosticMapping, IdentityMappingLeavesLineAndColumnUntouched)
{
    // with no custom pseudos the expanded text IS the source, so nothing moves
    const QString source = "main:\n    addi t0, t1, 5000\n";
    EditorTab tab(nullptr, nullptr);
    tab.setRawText(source);
    expandedPane(tab)->setPlainText(source);
    tab.setExpansionMapping({1, 2, 3});

    feedDiagnostic(tab, 2, 18, 22);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    EXPECT_EQ(selectionLine(input[0]), 2);
    const QTextCursor cursor = input[0].cursor;
    EXPECT_EQ(cursor.selectionStart() - cursor.block().position(), 17); // column 18
    EXPECT_EQ(cursor.selectionEnd() - cursor.block().position(), 21);   // column 22
}

TEST(EditorDiagnosticMapping, NoMappingRecordedBehavesAsOneToOne)
{
    const QString source = "main:\n    addi t0, t1, 5000\n";
    EditorTab tab(nullptr, nullptr);
    tab.setRawText(source);
    expandedPane(tab)->setPlainText(source);
    // deliberately never call setExpansionMapping()

    feedDiagnostic(tab, 2, 18, 22);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    EXPECT_EQ(selectionLine(input[0]), 2);
}

// --------------------------------------------------------------------------
// one original line producing several diagnostics
// --------------------------------------------------------------------------

TEST(EditorDiagnosticMapping, SeveralDiagnosticsCanMapOntoOneOriginalLine)
{
    //  "push t0" on original line 2 becomes expanded lines 2 and 3; an error
    //  on each must land on original line 2 twice.
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    push t0\n    ret\n");
    expandedPane(tab)->setPlainText("main:\naddi sp, sp, -8\nsd t0, 0(sp)\n    ret\n");
    tab.setExpansionMapping({1, 2, 2, 3});

    const auto path = std::filesystem::temp_directory_path() / "kites_map_multi.json";
    QFile file(QString::fromStdString(path.string()));
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(R"({"errorCode":1,"errors":[
        {"line":2,"column":1,"endColumn":5,"severity":"error","category":"Assembler","message":"first"},
        {"line":3,"column":1,"endColumn":3,"severity":"error","category":"Assembler","message":"second"}]})");
    file.close();
    tab.setErrorLinesFromFile(path);
    std::filesystem::remove(path);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 2);
    EXPECT_EQ(selectionLine(input[0]), 2);
    EXPECT_EQ(selectionLine(input[1]), 2);

    // both still land on their own distinct expanded lines
    const auto expanded = squiggles(expandedPane(tab));
    ASSERT_EQ(expanded.size(), 2);
    EXPECT_EQ(selectionLine(expanded[0]), 2);
    EXPECT_EQ(selectionLine(expanded[1]), 3);
}

// --------------------------------------------------------------------------
// byte columns -> UTF-16 offsets, and clamping
// --------------------------------------------------------------------------

TEST(EditorDiagnosticMapping, ByteColumnsAreConvertedToUtf16Offsets)
{
    //  The line from parser_error_column_test, with the e-acute as the two
    //  UTF-8 bytes 0xC3 0xA9:
    //
    //      "    .string \"caf<e-acute>\", \"x\""
    //
    //  By byte:  '"' of the second literal is column 22, its closing quote
    //  ends it at column 25. A QTextCursor counts UTF-16 units, where the
    //  e-acute is one unit, so the same token is at offsets 20..23 - one less
    //  at each end, matching the one extra byte before it.
    const QString line = QString::fromUtf8("    .string \"caf\xc3\xa9\", \"x\"");
    ASSERT_EQ(line.toUtf8().size(), 24);  // bytes
    ASSERT_EQ(line.size(), 23);           // UTF-16 units

    const QString source = "main:\n" + line + "\n";
    EditorTab tab(nullptr, nullptr);
    tab.setRawText(source);
    expandedPane(tab)->setPlainText(source);
    tab.setExpansionMapping({1, 2, 3});

    feedDiagnostic(tab, /*line=*/2, /*column=*/22, /*endColumn=*/25);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    const QTextCursor cursor = input[0].cursor;
    const int start = cursor.selectionStart() - cursor.block().position();
    const int end   = cursor.selectionEnd()   - cursor.block().position();
    EXPECT_EQ(start, 20) << "byte column 22 is UTF-16 offset 20";
    EXPECT_EQ(end, 23);
    EXPECT_EQ(cursor.selectedText(), QString("\"x\"")) << "must select exactly the token";
}

TEST(EditorDiagnosticMapping, AColumnPastTheEndOfTheLineIsClamped)
{
    //  "    ret" is 7 characters; a span of 40..50 cannot be honoured and must
    //  clamp to the end of the block rather than build an invalid cursor.
    const QString source = "main:\n    ret\n";
    EditorTab tab(nullptr, nullptr);
    tab.setRawText(source);
    expandedPane(tab)->setPlainText(source);
    tab.setExpansionMapping({1, 2, 3});

    feedDiagnostic(tab, 2, 40, 50);

    const auto input = squiggles(inputPane(tab));
    ASSERT_EQ(input.size(), 1);
    const QTextCursor cursor = input[0].cursor;
    EXPECT_EQ(cursor.block().text(), QString("    ret"));
    EXPECT_LE(cursor.selectionEnd() - cursor.block().position(), 7);
    EXPECT_GE(cursor.selectionStart() - cursor.block().position(), 0);
    // both ends clamp to the same place, so the span collapses and the
    // whole-line fallback takes over
    EXPECT_EQ(cursor.selectionStart() - cursor.block().position(), 0);
    EXPECT_EQ(cursor.selectionEnd() - cursor.block().position(), 7);
}
