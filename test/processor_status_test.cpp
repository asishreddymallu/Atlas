/**
 * @file processor_status_test.cpp
 * @brief The status bar says which processor is loaded.
 *
 * Before this stage nothing in the window chrome did: the title is the
 * fixed "Kites RISC-V Simulator" (`main_window.cpp:91`) and the status
 * bar held one permanent QLabel reading "Ready". The only way to find out
 * which of the five models was loaded was to reopen the chooser.
 *
 * The label must follow the processor that is actually LOADED, not the
 * one the dialog asked for. The two can differ: `changeProcessor` returns
 * early, leaving `m_currentProcessorType` alone, if the worker does not
 * stop (`processor_manager.cpp:62-66`). So the window reads the type back
 * from the manager after the call rather than trusting its own argument,
 * and the expected strings below are written out here independently of
 * `processorDescription`.
 */
#include "processor/processor_choice.h"
#include "processor/processor_manager.h"
#include "ui/main_window/main_window.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QStatusBar>

#include <string>
#include <vector>

using namespace Atlas;

namespace
{
QLabel *processorLabel(MainWindow &window)
{
    return window.findChild<QLabel *>("processorStatusLabel");
}

struct Expected
{
    ProcessorType type;
    const char   *text;
};

const std::vector<Expected> &expectedLabels()
{
    static const std::vector<Expected> rows{
        {ProcessorType::RVSS, "Processor: Single-cycle"},
        {ProcessorType::RV5Stage_NH_NF, "Processor: Pipeline, no hazard detection, no forwarding"},
        {ProcessorType::RV5Stage_H_NF, "Processor: Pipeline, hazard detection, no forwarding"},
        {ProcessorType::RV5Stage_NH_F, "Processor: Pipeline, no hazard detection, forwarding"},
        {ProcessorType::RV5Stage_H_F, "Processor: Pipeline, hazard detection + forwarding"},
    };
    return rows;
}
} // namespace

TEST(ProcessorStatusLabel, ItIsThereBeforeAnythingHasBeenChosen)
{
    MainWindow window;
    QLabel *label = processorLabel(window);
    ASSERT_NE(label, nullptr) << "no permanent widget named processorStatusLabel";

    // The window starts on RVSS: ProcessorManager's constructor default
    // (`processor_manager.h:35`), and nothing persists a choice.
    EXPECT_EQ(label->text().toStdString(), "Processor: Single-cycle");
}

TEST(ProcessorStatusLabel, ItIsAPermanentWidgetOfTheStatusBar)
{
    MainWindow window;
    QLabel *label = processorLabel(window);
    ASSERT_NE(label, nullptr);
    ASSERT_NE(window.statusBar(), nullptr);
    EXPECT_TRUE(window.statusBar()->isAncestorOf(label))
        << "the processor label is not in the status bar";

    // The "Ready" label is still there beside it.
    bool ready = false;
    for (QLabel *other : window.statusBar()->findChildren<QLabel *>())
        if (other->text() == "Ready")
            ready = true;
    EXPECT_TRUE(ready) << "the Ready label was displaced";
}

TEST(ProcessorStatusLabel, ItFollowsEveryProcessorChange)
{
    MainWindow window;
    QLabel *label = processorLabel(window);
    ASSERT_NE(label, nullptr);

    for (const Expected &row : expectedLabels())
    {
        window.processorChanged(row.type);
        QApplication::processEvents();
        EXPECT_EQ(label->text().toStdString(), std::string(row.text))
            << "after switching to " << row.text;
    }
}

TEST(ProcessorStatusLabel, TheManagerIsWhatTheLabelIsDescribing)
{
    // The window reads the type back from the manager rather than
    // trusting the argument it was handed, because `changeProcessor`
    // can decline (`processor_manager.cpp:62-66`). The window's own
    // manager is not a child of it (`main_window.cpp:98` constructs it
    // parentless and moves it to a worker thread), so it cannot be
    // reached from outside; this pins the other half of the claim - that
    // `getProcessorType()` really does track `changeProcessor`, and that
    // the five descriptions the label is built from come off it.
    ProcessorManager manager;
    EXPECT_EQ(manager.getProcessorType(), ProcessorType::RVSS);

    for (const Expected &row : expectedLabels())
    {
        manager.changeProcessor(row.type);
        EXPECT_EQ(manager.getProcessorType(), row.type);
        EXPECT_EQ("Processor: " + std::string(processorDescription(manager.getProcessorType())),
                  std::string(row.text));
    }
}

TEST(ProcessorStatusLabel, TheFiveLabelsAreAllDifferent)
{
    MainWindow window;
    QLabel *label = processorLabel(window);
    ASSERT_NE(label, nullptr);

    std::vector<std::string> seen;
    for (const Expected &row : expectedLabels())
    {
        window.processorChanged(row.type);
        QApplication::processEvents();
        const std::string text = label->text().toStdString();
        for (const std::string &other : seen)
            EXPECT_NE(text, other) << "two models give the same status line";
        seen.push_back(text);
    }
}
