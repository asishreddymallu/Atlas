/**
 * @file expanded_breakpoints_test.cpp
 * @brief Breakpoint dots in the Expanded pane sit on the right lines.
 *
 * The Expanded pane shows the source with custom pseudo-instructions
 * expanded, so its line numbers are not the Input pane's. It used to be
 * handed the Input pane's numbers verbatim, which put the dots on whatever
 * happened to be at those lines after expansion.
 *
 * `EditorTab::breakpointsForExpandedView()` reads the existing
 * expanded-line -> original-line mapping the other way. Most of what follows
 * drives that function with a hand-made mapping, because defining a real
 * custom pseudo-instruction means writing to the user's own instruction
 * file on disk, which a test has no business doing.
 */
#include "ui/editor_tab/editor_tab.h"
#include "ui/editor_tab/main_editor.h"
#include "ui/main_window/main_window.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QPlainTextEdit>
#include <QPushButton>

using namespace Kites;

namespace
{
struct Tab
{
    MainWindow window;
    EditorTab *tab    {nullptr};
    Editor    *editor {nullptr};

    Tab()
    {
        window.resize(1200, 800);
        window.show();
        QApplication::processEvents();
        tab = window.findChild<EditorTab *>();
        for (QPlainTextEdit *pane : tab->findChildren<QPlainTextEdit *>())
            if (!pane->isReadOnly())
                editor = static_cast<Editor *>(pane);
    }

    QPlainTextEdit *expandedPane() const
    {
        for (QPlainTextEdit *pane : tab->findChildren<QPlainTextEdit *>())
            if (pane->objectName() == "expandedTextEdit")
                return pane;
        return nullptr;
    }

    QList<int> dots() const
    {
        QList<int> out;
        for (uint64_t line : static_cast<Editor *>(expandedPane())->getBreakpoints())
            out << int(line);
        return out;
    }

    void setBreakpoints(const QList<int> &lines)
    {
        editor->setBreakpoints({});
        for (int line : lines)
            editor->toggleBreakpointAtLine(line);
    }

    QList<int> mapThrough(const std::vector<int> &expandedToOriginal) const
    {
        QList<int> out;
        for (uint64_t line : tab->breakpointsForExpandedView(expandedToOriginal))
            out << int(line);
        return out;
    }
};
} // namespace

TEST(ExpandedBreakpoints, APseudoInstructionExpandingToSeveralLinesGetsOneDotOnTheFirst)
{
    Tab bench;
    bench.editor->setPlainText("li t0, 1\nbigmacro t1\nadd t2, t0, t1\naddi t3, t2, 1");
    bench.setBreakpoints({2, 4});

    // Line 2 expands to three lines; everything else is one for one.
    //   expanded 1 <- 1, expanded 2,3,4 <- 2, expanded 5 <- 3, expanded 6 <- 4
    const std::vector<int> mapping = {1, 2, 2, 2, 3, 4};

    // The dot for source line 2 goes on expanded line 2 and nowhere else;
    // source line 4 is expanded line 6.
    EXPECT_EQ(bench.mapThrough(mapping), (QList<int>{2, 6}));
}

TEST(ExpandedBreakpoints, ABreakpointOnALineThatProducesNoExpandedLineGetsNoDot)
{
    Tab bench;
    bench.editor->setPlainText("# just a comment\nli t0, 1\n\nadd t1, t0, t0");
    bench.setBreakpoints({1, 2, 3, 4});

    // An expansion that drops the comment and the blank line: nothing in the
    // mapping points back at source lines 1 or 3.
    const std::vector<int> mapping = {2, 4};

    // Only the two lines that survive the expansion carry a dot.
    EXPECT_EQ(bench.mapThrough(mapping), (QList<int>{1, 2}));
}

TEST(ExpandedBreakpoints, WithNoMappingRecordedTheTwoPanesAgreeLineForLine)
{
    Tab bench;
    bench.editor->setPlainText("a\nb\nc\nd\ne");
    bench.setBreakpoints({2, 4});

    // An empty mapping means nothing expanded, so the numbers pass through.
    EXPECT_EQ(bench.mapThrough({}), (QList<int>{2, 4}));

    // And an identity mapping says the same thing the long way round.
    EXPECT_EQ(bench.mapThrough({1, 2, 3, 4, 5}), (QList<int>{2, 4}));
}

TEST(ExpandedBreakpoints, OnlyTheFirstExpandedLineIsTakenEvenWhenSeveralMatch)
{
    Tab bench;
    bench.editor->setPlainText("one\ntwo");
    bench.setBreakpoints({1});

    // Four expanded lines all came from source line 1.
    EXPECT_EQ(bench.mapThrough({1, 1, 1, 1, 2}), (QList<int>{1}))
        << "a dot per expanded line, instead of one on the first";
}

TEST(ExpandedBreakpoints, ShowingTheExpandedViewPutsTheDotsThereAndChangesNothingElse)
{
    Tab bench;
    const QString source = "li t0, 1\nadd t1, t0, t0\nsub t2, t1, t0\naddi t3, t2, 1";
    bench.editor->setPlainText(source);
    bench.setBreakpoints({2, 4});

    auto *button = bench.tab->findChild<QPushButton *>("expandedViewButton");
    ASSERT_NE(button, nullptr);
    button->setChecked(true);
    QApplication::processEvents();

    QPlainTextEdit *expanded = bench.expandedPane();
    ASSERT_NE(expanded, nullptr);

    if (expanded->toPlainText() != source)
    {
        // Somebody has custom pseudo-instructions defined on this machine,
        // so the two texts are not line for line and this case cannot say
        // anything useful. It skips rather than asserting something false.
        GTEST_SKIP() << "custom pseudo-instructions are defined; the texts differ";
    }

    EXPECT_EQ(bench.dots(), (QList<int>{2, 4}))
        << "the Expanded pane's dots are not on the same lines";

    // And nothing that leaves the editor changed.
    std::vector<uint64_t> toTheVm = bench.editor->getBreakpoints();
    EXPECT_EQ(toTheVm, (std::vector<uint64_t>{2, 4}))
        << "showing the Expanded view changed what the VM would be given";
    EXPECT_TRUE(expanded->isReadOnly()) << "the Expanded pane stopped being read-only";
}
