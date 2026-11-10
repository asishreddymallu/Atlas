/**
 * @file problems_navigation_test.cpp
 * @brief Dock navigation lands exactly where the Input-pane squiggle is.
 *
 * EditorTab::goToDiagnostic and applyDiagnostics share inputTargetFor() and
 * makeCursor(), so the two cannot drift apart; these tests assert that by
 * comparing the cursor the dock produces against the squiggle's selection.
 */
#include "language_service/diagnostic.h"
#include "ui/editor_tab/editortab.h"
#include "ui/problems_panel/problems_dock.h"
#include "ui/problems_panel/problems_model.h"

#include <gtest/gtest.h>

#include <QFile>
#include <QPlainTextEdit>
#include <QStackedWidget>
#include <QTableView>
#include <filesystem>
#include <QTextEdit>

using namespace Kites;

namespace
{
QPlainTextEdit *inputPane(EditorTab &tab)
{
    return tab.findChild<QPlainTextEdit *>("assemblyTextEdit");
}
QPlainTextEdit *expandedPane(EditorTab &tab)
{
    return tab.findChild<QPlainTextEdit *>("expandedTextEdit");
}

QList<QTextEdit::ExtraSelection> squiggles(QPlainTextEdit *pane)
{
    QList<QTextEdit::ExtraSelection> result;
    for (const QTextEdit::ExtraSelection &selection : pane->extraSelections())
        if (selection.format.underlineStyle() == QTextCharFormat::WaveUnderline)
            result.append(selection);
    return result;
}

/// Feeds diagnostics through the public path EditorTab uses after a Run.
void feed(EditorTab &tab, int line, int startColumn, int endColumn,
          const char *message = "boom")
{
    const auto path = std::filesystem::temp_directory_path() / "kites_nav_errors.json";
    QFile file(QString::fromStdString(path.string()));
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QString(R"({"errorCode":1,"errors":[{"line":%1,"column":%2,"endColumn":%3,)"
                       R"("severity":"error","category":"Assembler","message":"%4"}]})")
                   .arg(line).arg(startColumn).arg(endColumn).arg(message).toUtf8());
    file.close();
    tab.setErrorLinesFromFile(path);
    std::filesystem::remove(path);
}

Diagnostic make(int line, int startColumn, int endColumn, const char *message = "boom")
{
    Diagnostic diagnostic;
    diagnostic.line        = line;
    diagnostic.startColumn = startColumn;
    diagnostic.endColumn   = endColumn;
    diagnostic.severity    = DiagnosticSeverity::Error;
    diagnostic.category    = DiagnosticCategory::Assembler;
    diagnostic.message     = QString::fromUtf8(message);
    return diagnostic;
}
} // namespace

TEST(ProblemsNavigation, NormalLineSelectsExactlyTheToken)
{
    const QString source = "main:\n    addi t0, t1, 5000\n";
    EditorTab tab(nullptr, nullptr);
    tab.setRawText(source);
    expandedPane(tab)->setPlainText(source);
    tab.setExpansionMapping({1, 2, 3});

    const Diagnostic diagnostic = make(2, 18, 22, "Immediate value out of range");
    feed(tab, 2, 18, 22, "Immediate value out of range");

    const auto marks = squiggles(inputPane(tab));
    ASSERT_EQ(marks.size(), 1);
    EXPECT_EQ(marks[0].cursor.selectedText(), QString("5000"));

    ASSERT_TRUE(tab.goToDiagnostic(diagnostic));
    const QTextCursor cursor = inputPane(tab)->textCursor();
    EXPECT_EQ(cursor.blockNumber() + 1, 2);
    EXPECT_EQ(cursor.selectedText(), QString("5000"));
    EXPECT_EQ(cursor.selectionStart(), marks[0].cursor.selectionStart())
        << "navigation must select exactly what the squiggle underlines";
    EXPECT_EQ(cursor.selectionEnd(), marks[0].cursor.selectionEnd());
}

TEST(ProblemsNavigation, GeneratedLineSelectsTheWholeOriginalLine)
{
    //  the bigadd program: expanded line 4 is generated text, so the columns
    //  mean nothing in the source and both the squiggle and the jump fall back
    //  to the whole line
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    push t0\n    bigadd t3\n    ret\n");
    expandedPane(tab)->setPlainText(
        "main:\naddi sp, sp, -8\nsd t0, 0(sp)\naddi t3, t1, 5000\n    ret\n");
    tab.setExpansionMapping({1, 2, 2, 3, 4, 5});

    const Diagnostic diagnostic = make(4, 14, 18);
    feed(tab, 4, 14, 18);

    const auto marks = squiggles(inputPane(tab));
    ASSERT_EQ(marks.size(), 1);
    EXPECT_EQ(marks[0].cursor.selectedText(), QString("    bigadd t3"));

    ASSERT_TRUE(tab.goToDiagnostic(diagnostic));
    const QTextCursor cursor = inputPane(tab)->textCursor();
    EXPECT_EQ(cursor.blockNumber() + 1, 3) << "the source line, not the expanded one";
    EXPECT_EQ(cursor.selectedText(), QString("    bigadd t3"));
    EXPECT_EQ(cursor.selectionStart(), marks[0].cursor.selectionStart());
    EXPECT_EQ(cursor.selectionEnd(), marks[0].cursor.selectionEnd());
}

TEST(ProblemsNavigation, NavigatingSwitchesAwayFromTheExpandedView)
{
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    addi t0, t1, 5000\n");
    expandedPane(tab)->setPlainText("main:\n    addi t0, t1, 5000\n");
    tab.setExpansionMapping({1, 2, 3});
    auto *stack = tab.findChild<QStackedWidget *>("stackedWidget");
    ASSERT_NE(stack, nullptr);

    tab.switchToExpandedView();
    ASSERT_EQ(stack->currentIndex(), 1) << "precondition: showing the Expanded view";

    ASSERT_TRUE(tab.goToDiagnostic(make(2, 18, 22)));
    EXPECT_EQ(stack->currentIndex(), 0) << "must leave the read-only generated view";
    EXPECT_EQ(stack->currentWidget(), inputPane(tab)->parentWidget());
}

TEST(ProblemsNavigation, ADiagnosticWithNoLineIsANoOp)
{
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    ret\n");
    expandedPane(tab)->setPlainText("main:\n    ret\n");

    const int before = inputPane(tab)->textCursor().position();
    EXPECT_FALSE(tab.goToDiagnostic(make(0, 0, 0)));
    EXPECT_EQ(inputPane(tab)->textCursor().position(), before) << "cursor must not move";
}

TEST(ProblemsNavigation, ALineBeyondTheDocumentIsANoOp)
{
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    ret\n");
    expandedPane(tab)->setPlainText("main:\n    ret\n");

    const int before = inputPane(tab)->textCursor().position();
    EXPECT_FALSE(tab.goToDiagnostic(make(99, 1, 4)));
    EXPECT_EQ(inputPane(tab)->textCursor().position(), before);
}

// --------------------------------------------------------------------------
// the dock itself
// --------------------------------------------------------------------------

TEST(ProblemsDockBehaviour, ActivatingANavigableRowEmitsTheDiagnostic)
{
    ProblemsDock dock;
    dock.setDiagnostics({make(2, 18, 22, "Immediate value out of range")}, /*lines=*/10);

    int emitted = 0;
    QString message;
    QObject::connect(&dock, &ProblemsDock::diagnosticActivated,
                     [&](const Diagnostic &d) { ++emitted; message = d.message; });

    auto *view = dock.findChild<QTableView *>("problemsView");
    ASSERT_NE(view, nullptr);
    emit view->doubleClicked(dock.model()->index(0, 0));

    EXPECT_EQ(emitted, 1);
    EXPECT_EQ(message, QString("Immediate value out of range"));
}

TEST(ProblemsDockBehaviour, ActivatingAnUnreachableRowEmitsNothing)
{
    ProblemsDock dock;
    dock.setDiagnostics({make(99, 1, 4, "past the end")}, /*lines=*/10);

    int emitted = 0;
    QObject::connect(&dock, &ProblemsDock::diagnosticActivated,
                     [&](const Diagnostic &) { ++emitted; });

    auto *view = dock.findChild<QTableView *>("problemsView");
    emit view->doubleClicked(dock.model()->index(0, 0));

    EXPECT_EQ(emitted, 0) << "no jump, and no crash";
    EXPECT_EQ(dock.problemCount(), 1) << "the row is still listed";
}

TEST(ProblemsDockBehaviour, TitleCarriesThePostDedupCountAndEmptiesCleanly)
{
    ProblemsDock dock;
    EXPECT_EQ(dock.windowTitle(), QString("Problems"));

    // the 615/662 pair plus one other problem: three in, two rows out
    dock.setDiagnostics({make(1, 1, 3, "Invalid token: Expected .data, .text, or <opcode>"),
                         make(1, 1, 3, "Invalid token: Expected .data, .text, .bss or <opcode>"),
                         make(4, 2, 6, "something else")},
                        /*lines=*/10);
    EXPECT_EQ(dock.problemCount(), 2);
    EXPECT_EQ(dock.windowTitle(), QString("Problems (2)"));

    dock.setDiagnostics({}, 10);
    EXPECT_EQ(dock.problemCount(), 0);
    EXPECT_EQ(dock.windowTitle(), QString("Problems"));
}

TEST(ProblemsDockBehaviour, ResetErrorLinesReachesTheDockWithAnEmptyList)
{
    // the success path and resetErrorLines both have to clear the dock
    EditorTab tab(nullptr, nullptr);
    tab.setRawText("main:\n    addi t0, t1, 5000\n");
    expandedPane(tab)->setPlainText("main:\n    addi t0, t1, 5000\n");

    int emissions = 0;
    int lastCount = -1;
    QObject::connect(&tab, &EditorTab::diagnosticsChangedSignal,
                     [&](const QVector<Diagnostic> &d) { ++emissions; lastCount = d.size(); });

    feed(tab, 2, 18, 22);
    EXPECT_EQ(lastCount, 1);

    tab.resetErrorLines();
    EXPECT_EQ(lastCount, 0) << "resetErrorLines must clear the dock";
    EXPECT_EQ(emissions, 2);
}
