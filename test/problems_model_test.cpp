/**
 * @file problems_model_test.cpp
 * @brief ProblemsModel: deduplication, ordering, rendering and navigability.
 *
 * No widgets here - the model is deliberately independent of the dock.
 */
#include "language_service/diagnostic.h"
#include "ui/problems_panel/problems_model.h"

#include <gtest/gtest.h>

#include <QVector>

using namespace Kites;

namespace
{
Diagnostic make(int line, int startColumn, int endColumn, const char *message,
                DiagnosticSeverity severity = DiagnosticSeverity::Error,
                DiagnosticCategory category = DiagnosticCategory::Assembler,
                int originalLine = 0)
{
    Diagnostic diagnostic;
    diagnostic.line         = line;
    diagnostic.originalLine = originalLine;
    diagnostic.startColumn  = startColumn;
    diagnostic.endColumn    = endColumn;
    diagnostic.severity     = severity;
    diagnostic.category     = category;
    diagnostic.message      = QString::fromUtf8(message);
    return diagnostic;
}

QString cell(const ProblemsModel &model, int row, ProblemsModel::Column column)
{
    return model.data(model.index(row, column), Qt::DisplayRole).toString();
}
} // namespace

// --------------------------------------------------------------------------
// deduplication
// --------------------------------------------------------------------------

TEST(ProblemsModel, StrayTokenPairFromTheTwoParserPassesCollapsesToOneRow)
{
    // parser.cpp:615 and :662 both reject the same stray token, at the same
    // line and column, with slightly different wording. One problem, one row,
    // and the first message wins.
    ProblemsModel model;
    model.setDiagnostics({
        make(1, 1, 3, "Invalid token: Expected .data, .text, or <opcode> or <label>",
             DiagnosticSeverity::Error, DiagnosticCategory::Syntax),
        make(1, 1, 3, "Invalid token: Expected .data, .text, .bss or <opcode> or <label>",
             DiagnosticSeverity::Error, DiagnosticCategory::Syntax),
    });

    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(model.problemCount(), 1);
    EXPECT_EQ(cell(model, 0, ProblemsModel::MessageColumn),
              QString("Invalid token: Expected .data, .text, or <opcode> or <label>"));
}

TEST(ProblemsModel, TwoDistinctColumnZeroErrorsOnOneLineStayTwoRows)
{
    // The six second-pass sites report no column. Without the message in the
    // key these would collapse into one row and hide a real problem.
    ProblemsModel model;
    model.setDiagnostics({
        make(4, 0, 0, "Immediate value out of range"),
        make(4, 0, 0, "Invalid label reference: Label reference not found"),
    });

    EXPECT_EQ(model.rowCount(), 2);
    EXPECT_EQ(cell(model, 0, ProblemsModel::MessageColumn),
              QString("Immediate value out of range"));
    EXPECT_EQ(cell(model, 1, ProblemsModel::MessageColumn),
              QString("Invalid label reference: Label reference not found"));
}

TEST(ProblemsModel, IdenticalColumnZeroErrorsOnOneLineStillCollapse)
{
    ProblemsModel model;
    model.setDiagnostics({
        make(4, 0, 0, "Immediate value out of range"),
        make(4, 0, 0, "Immediate value out of range"),
    });
    EXPECT_EQ(model.rowCount(), 1);
}

TEST(ProblemsModel, DedupKeyUsesTheExpandedLineNotTheMappedOriginal)
{
    // two different expanded lines that map back to the SAME original line are
    // two different problems and must not be merged
    ProblemsModel model;
    model.setDiagnostics({
        make(2, 1, 5, "first",  DiagnosticSeverity::Error, DiagnosticCategory::Assembler, 2),
        make(3, 1, 5, "second", DiagnosticSeverity::Error, DiagnosticCategory::Assembler, 2),
    });
    EXPECT_EQ(model.rowCount(), 2);
    // both navigate to the same source line
    EXPECT_EQ(model.navigationLine(0), 2);
    EXPECT_EQ(model.navigationLine(1), 2);
}

TEST(ProblemsModel, DifferingSeverityOrCategoryAreNotMerged)
{
    ProblemsModel model;
    model.setDiagnostics({
        make(7, 3, 6, "same text", DiagnosticSeverity::Error,   DiagnosticCategory::Syntax),
        make(7, 3, 6, "same text", DiagnosticSeverity::Warning, DiagnosticCategory::Syntax),
        make(7, 3, 6, "same text", DiagnosticSeverity::Error,   DiagnosticCategory::Assembler),
    });
    EXPECT_EQ(model.rowCount(), 3);
}

// --------------------------------------------------------------------------
// ordering
// --------------------------------------------------------------------------

TEST(ProblemsModel, RowsAreOrderedByLineThenColumnAndTheSortIsStable)
{
    ProblemsModel model;
    model.setDiagnostics({
        make(9, 5,  9, "nine"),
        make(2, 18, 22, "two late"),
        make(2, 5,  9, "two early"),
        make(5, 0,  0, "five no column"),
        make(5, 0,  0, "five no column, different text"),
    });

    ASSERT_EQ(model.rowCount(), 5);
    EXPECT_EQ(cell(model, 0, ProblemsModel::MessageColumn), QString("two early"));
    EXPECT_EQ(cell(model, 1, ProblemsModel::MessageColumn), QString("two late"));
    // equal keys keep the order they arrived in
    EXPECT_EQ(cell(model, 2, ProblemsModel::MessageColumn), QString("five no column"));
    EXPECT_EQ(cell(model, 3, ProblemsModel::MessageColumn),
              QString("five no column, different text"));
    EXPECT_EQ(cell(model, 4, ProblemsModel::MessageColumn), QString("nine"));
}

// --------------------------------------------------------------------------
// rendering
// --------------------------------------------------------------------------

TEST(ProblemsModel, ColumnsRenderSeverityLocationMessageAndCategory)
{
    ProblemsModel model;
    model.setDiagnostics({make(12, 5, 9, "Invalid register", DiagnosticSeverity::Warning,
                               DiagnosticCategory::Syntax)});

    EXPECT_EQ(model.columnCount(), ProblemsModel::ColumnCount);
    EXPECT_EQ(cell(model, 0, ProblemsModel::SeverityColumn), QString("Warning"));
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("12:5"));
    EXPECT_EQ(cell(model, 0, ProblemsModel::MessageColumn),  QString("Invalid register"));
    EXPECT_EQ(cell(model, 0, ProblemsModel::CategoryColumn), QString("Syntax"));
}

TEST(ProblemsModel, SeverityTextIsShownSoTheRowDoesNotRelyOnColourAlone)
{
    ProblemsModel model;
    model.setDiagnostics({
        make(1, 1, 2, "e", DiagnosticSeverity::Error),
        make(2, 1, 2, "w", DiagnosticSeverity::Warning),
        make(3, 1, 2, "i", DiagnosticSeverity::Information),
        make(4, 1, 2, "h", DiagnosticSeverity::Hint),
    });
    EXPECT_EQ(cell(model, 0, ProblemsModel::SeverityColumn), QString("Error"));
    EXPECT_EQ(cell(model, 1, ProblemsModel::SeverityColumn), QString("Warning"));
    EXPECT_EQ(cell(model, 2, ProblemsModel::SeverityColumn), QString("Info"));
    EXPECT_EQ(cell(model, 3, ProblemsModel::SeverityColumn), QString("Hint"));

    // and every row still carries a dot
    for (int row = 0; row < 4; ++row)
    {
        const QVariant icon =
            model.data(model.index(row, ProblemsModel::SeverityColumn), Qt::DecorationRole);
        EXPECT_FALSE(icon.isNull()) << "row " << row << " has no severity dot";
    }
}

TEST(ProblemsModel, LocationOmitsTheColumnWhenTheSpanIsUnknown)
{
    ProblemsModel model;
    model.setDiagnostics({make(4, 0, 0, "no column")});
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("4"));
}

// --------------------------------------------------------------------------
// navigability
// --------------------------------------------------------------------------

TEST(ProblemsModel, LineZeroStaysVisibleButIsNotNavigable)
{
    ProblemsModel model;
    model.setDiagnostics({make(0, 0, 0, "nowhere")});

    ASSERT_EQ(model.rowCount(), 1); // the row is still shown
    EXPECT_FALSE(model.isNavigable(0));
    EXPECT_EQ(model.navigationLine(0), 0);
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("—"));
    EXPECT_EQ(cell(model, 0, ProblemsModel::MessageColumn), QString("nowhere"));
}

TEST(ProblemsModel, ALineBeyondTheDocumentIsNotNavigable)
{
    ProblemsModel model;
    model.setDiagnostics({make(2, 1, 4, "inside"), make(99, 1, 4, "past the end")});
    model.setDocumentLineCount(10);

    EXPECT_TRUE(model.isNavigable(0));
    EXPECT_FALSE(model.isNavigable(1));
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("2:1"));
    EXPECT_EQ(cell(model, 1, ProblemsModel::LocationColumn), QString("—"));
    EXPECT_EQ(model.rowCount(), 2) << "the row stays visible, it just cannot be jumped to";
}

TEST(ProblemsModel, AnUnknownDocumentLengthDisablesTheBoundsCheck)
{
    ProblemsModel model;
    model.setDiagnostics({make(99, 1, 4, "past the end of a document we have not measured")});
    // setDocumentLineCount never called, so it stays 0 = unknown
    EXPECT_TRUE(model.isNavigable(0));
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("99:1"));
}

TEST(ProblemsModel, NavigationUsesTheMappedOriginalLineWhenThereIsOne)
{
    ProblemsModel model;
    model.setDiagnostics({make(6, 18, 22, "mapped", DiagnosticSeverity::Error,
                               DiagnosticCategory::Assembler, /*originalLine=*/4)});
    EXPECT_EQ(model.navigationLine(0), 4);
    EXPECT_EQ(cell(model, 0, ProblemsModel::LocationColumn), QString("4:18"));
}

// --------------------------------------------------------------------------
// lifecycle
// --------------------------------------------------------------------------

TEST(ProblemsModel, ClearEmptiesTheModel)
{
    ProblemsModel model;
    model.setDiagnostics({make(1, 1, 2, "a"), make(2, 1, 2, "b")});
    ASSERT_EQ(model.rowCount(), 2);

    model.clear();
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.problemCount(), 0);
    EXPECT_EQ(model.diagnosticAt(0), nullptr);
}

TEST(ProblemsModel, SettingAnEmptyListEmptiesTheModel)
{
    ProblemsModel model;
    model.setDiagnostics({make(1, 1, 2, "a")});
    ASSERT_EQ(model.rowCount(), 1);

    model.setDiagnostics({});
    EXPECT_EQ(model.rowCount(), 0);
    EXPECT_EQ(model.problemCount(), 0);
}

TEST(ProblemsModel, OutOfRangeAccessIsSafe)
{
    ProblemsModel model;
    EXPECT_EQ(model.diagnosticAt(0), nullptr);
    EXPECT_EQ(model.diagnosticAt(-1), nullptr);
    EXPECT_FALSE(model.isNavigable(0));
    EXPECT_EQ(model.navigationLine(5), 0);
    EXPECT_FALSE(model.data(model.index(0, 0), Qt::DisplayRole).isValid());
}
