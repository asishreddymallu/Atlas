/**
 * @file processor_choice_test.cpp
 * @brief The chooser's mapping, with no dialog and no window.
 *
 * The old chooser turned a selection into a `ProcessorType` by comparing
 * the clicked row's TEXT against five literal strings
 * (`processor_dialog.cpp:19-48` before this stage), and recovered the
 * current selection with `topLevelItem(static_cast<int>(currentVMType))`,
 * which silently assumed the .ui's row order matched the enum's. Neither
 * could be tested without building a dialog, so neither was.
 *
 * `processor_choice.h` is that mapping as four free functions with no Qt
 * in them, so the relation between three check marks and five models is
 * pinned here, once, and the dialog is left with nothing to decide.
 *
 * The expected strings below are written out in full rather than built by
 * calling the functions under test. A test that asks the code what it
 * says and then agrees cannot fail; these are an independent copy, and
 * the stage's negative control (c) depends on that.
 */
#include "processor/processor_choice.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Atlas;

namespace
{
/// Every (pipeline, hazard, forwarding) the dialog can produce, and the
/// one model it must mean. Eight rows, not five, because Single-cycle
/// has to swallow both booleans.
struct Row
{
    bool          pipeline;
    bool          hazard;
    bool          forwarding;
    ProcessorType type;
    const char   *what;
};

const std::vector<Row> &allCombinations()
{
    static const std::vector<Row> rows{
        {false, false, false, ProcessorType::RVSS, "single-cycle, neither box"},
        {false, false, true, ProcessorType::RVSS, "single-cycle, forwarding ticked"},
        {false, true, false, ProcessorType::RVSS, "single-cycle, hazard ticked"},
        {false, true, true, ProcessorType::RVSS, "single-cycle, both ticked"},
        {true, false, false, ProcessorType::RV5Stage_NH_NF, "pipeline, neither"},
        {true, true, false, ProcessorType::RV5Stage_H_NF, "pipeline, hazard only"},
        {true, false, true, ProcessorType::RV5Stage_NH_F, "pipeline, forwarding only"},
        {true, true, true, ProcessorType::RV5Stage_H_F, "pipeline, both"},
    };
    return rows;
}

const std::vector<ProcessorType> &allTypes()
{
    static const std::vector<ProcessorType> types{
        ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_H_NF,
        ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_H_F};
    return types;
}

/// Only for failure messages; the production code must not need it.
std::string nameOf(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RVSS:           return "RVSS";
    case ProcessorType::RV5Stage_NH_NF: return "RV5Stage_NH_NF";
    case ProcessorType::RV5Stage_H_NF:  return "RV5Stage_H_NF";
    case ProcessorType::RV5Stage_NH_F:  return "RV5Stage_NH_F";
    case ProcessorType::RV5Stage_H_F:   return "RV5Stage_H_F";
    default:                            return "?";
    }
}
} // namespace

TEST(ProcessorChoice, EveryCombinationOfTheThreeControlsMapsToOneModel)
{
    for (const Row &row : allCombinations())
    {
        EXPECT_EQ(processorTypeFor(row.pipeline, row.hazard, row.forwarding), row.type)
            << row.what << ": got " << nameOf(processorTypeFor(row.pipeline, row.hazard,
                                                               row.forwarding))
            << ", wanted " << nameOf(row.type);
    }
}

TEST(ProcessorChoice, SingleCycleIgnoresBothBooleans)
{
    // The four single-cycle rows above say this, but it is the one
    // property the dialog relies on when it greys the boxes and leaves
    // them ticked, so it is stated on its own.
    for (bool hazard : {false, true})
        for (bool forwarding : {false, true})
            EXPECT_EQ(processorTypeFor(false, hazard, forwarding), ProcessorType::RVSS)
                << "hazard=" << hazard << " forwarding=" << forwarding;
}

TEST(ProcessorChoice, TheInverseRoundTripsForEveryProcessorType)
{
    for (ProcessorType type : allTypes())
    {
        const ProcessorChoice choice = processorChoiceFor(type);
        EXPECT_EQ(processorTypeFor(choice.pipeline, choice.hazard, choice.forwarding), type)
            << nameOf(type) << " did not survive the round trip: pipeline=" << choice.pipeline
            << " hazard=" << choice.hazard << " forwarding=" << choice.forwarding;
    }
}

TEST(ProcessorChoice, TheFourPipelineModelsRoundTripThroughTheirOwnBooleans)
{
    // Stronger than the round trip above: for the four pipeline models
    // the booleans are not free, they are the model's own two features.
    const struct
    {
        ProcessorType type;
        bool          hazard;
        bool          forwarding;
    } expected[] = {
        {ProcessorType::RV5Stage_NH_NF, false, false},
        {ProcessorType::RV5Stage_H_NF, true, false},
        {ProcessorType::RV5Stage_NH_F, false, true},
        {ProcessorType::RV5Stage_H_F, true, true},
    };

    for (const auto &row : expected)
    {
        const ProcessorChoice choice = processorChoiceFor(row.type);
        EXPECT_TRUE(choice.pipeline) << nameOf(row.type) << " is not a pipeline";
        EXPECT_EQ(choice.hazard, row.hazard) << nameOf(row.type) << ": hazard detection";
        EXPECT_EQ(choice.forwarding, row.forwarding) << nameOf(row.type) << ": forwarding";
    }
}

TEST(ProcessorChoice, SingleCycleRemembersAPipelineWorthSwitchingTo)
{
    // Opened on Single-cycle the two boxes are greyed but keep a value,
    // and that value decides where ticking Pipeline lands. It lands on
    // hazard detection + forwarding, not on the variant that needs
    // hand-written nops (known_issues 77).
    const ProcessorChoice choice = processorChoiceFor(ProcessorType::RVSS);
    EXPECT_FALSE(choice.pipeline);
    EXPECT_TRUE(choice.hazard);
    EXPECT_TRUE(choice.forwarding);
    EXPECT_EQ(processorTypeFor(true, choice.hazard, choice.forwarding),
              ProcessorType::RV5Stage_H_F);
}

TEST(ProcessorChoice, TheFiveDescriptionsAreTheOnesTheStatusBarShows)
{
    EXPECT_EQ(std::string(processorDescription(ProcessorType::RVSS)), "Single-cycle");
    EXPECT_EQ(std::string(processorDescription(ProcessorType::RV5Stage_NH_NF)),
              "Pipeline, no hazard detection, no forwarding");
    EXPECT_EQ(std::string(processorDescription(ProcessorType::RV5Stage_H_NF)),
              "Pipeline, hazard detection, no forwarding");
    EXPECT_EQ(std::string(processorDescription(ProcessorType::RV5Stage_NH_F)),
              "Pipeline, no hazard detection, forwarding");
    EXPECT_EQ(std::string(processorDescription(ProcessorType::RV5Stage_H_F)),
              "Pipeline, hazard detection + forwarding");
}

TEST(ProcessorChoice, EveryDescriptionIsDistinct)
{
    std::vector<std::string> seen;
    for (ProcessorType type : allTypes())
    {
        const std::string text = processorDescription(type);
        for (const std::string &other : seen)
            EXPECT_NE(text, other) << "two models describe themselves the same way";
        seen.push_back(text);
    }
}

TEST(ProcessorChoice, TheNoteSaysWhatEachCombinationCosts)
{
    // The wording is checked against the code it describes:
    //   forwarding on  -> rv5s_hdu.cpp:82-104 stalls only on load-use
    //   forwarding off -> rv5s_hdu.cpp:106-117 stalls to write-back
    //   no hazard unit -> rv5s_processor_nh_nf.cpp / nh_f.cpp never call
    //                     check_data_hazard; known_issues 77's "0 on both"
    //                     was corrected there (nh-f forwards; only a load
    //                     needs a nop).
    // U5: the pipeline notes now state measured nop counts; the numbers
    // are pinned against the machine by processor_note_measure_test.cpp.
    EXPECT_EQ(std::string(processorNote(false, true, true)),
              "Every instruction finishes in one cycle; there are no pipeline hazards.");
    EXPECT_EQ(std::string(processorNote(true, true, true)),
              "Forwards; stalls 1 cycle on a load-use.\n"
              "Nothing to pad.");
    EXPECT_EQ(std::string(processorNote(true, true, false)),
              "Stalls until the value reaches write-back.\n"
              "Nothing to pad.");
    EXPECT_EQ(std::string(processorNote(true, false, true)),
              "No stalls: a load needs 1 nop before use.\n"
              "All else is forwarded; no nops.");
    EXPECT_EQ(std::string(processorNote(true, false, false)),
              "Ready at write-back: a reader needs 2 nops.\n"
              "Large li and la give wrong results.");
}

TEST(ProcessorChoice, TheSingleCycleNoteDoesNotDependOnTheBoxes)
{
    const std::string note = processorNote(false, true, true);
    for (bool hazard : {false, true})
        for (bool forwarding : {false, true})
            EXPECT_EQ(std::string(processorNote(false, hazard, forwarding)), note)
                << "the greyed boxes changed the single-cycle note";
}
