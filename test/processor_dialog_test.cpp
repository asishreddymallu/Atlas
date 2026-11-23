/**
 * @file processor_dialog_test.cpp
 * @brief The chooser dialog, offscreen: what it shows and what it emits.
 *
 * Two radio buttons and two checkboxes over five models. The three things
 * that can go wrong are all pinned here:
 *
 *   - the dialog opens showing something other than the processor that is
 *     actually loaded (the old dialog preselected by
 *     `topLevelItem(static_cast<int>(currentVMType))`, i.e. by assuming
 *     the .ui's row order matched the enum's);
 *   - Single-cycle greys the two boxes but LOSES their values, so
 *     Single -> Pipeline does not come back to where it was;
 *   - OK emits a different model from the one on screen (the old dialog
 *     recovered it by string-matching the selected row's text, so a
 *     reworded row emitted nothing at all).
 *
 * The widgets are reached by objectName, because `ProcessorDialog::ui` is
 * private and a test has no business being a friend of it.
 */
#include "processor/processor_choice.h"
#include "ui/dialogs/processor_dialog.h"

#include <gtest/gtest.h>

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>

#include <string>
#include <vector>

using namespace Atlas;

namespace
{
const std::vector<ProcessorType> &allTypes()
{
    static const std::vector<ProcessorType> types{
        ProcessorType::RVSS, ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_H_NF,
        ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_H_F};
    return types;
}

std::string nameOf(ProcessorType type)
{
    switch (type)
    {
    case ProcessorType::RVSS:           return "RVSS";
    case ProcessorType::RV5Stage_NH_NF: return "RV5Stage_NH_NF";
    case ProcessorType::RV5Stage_H_NF:  return "RV5Stage_H_NF";
    case ProcessorType::RV5Stage_NH_F:  return "RV5Stage_NH_F";
    case ProcessorType::RV5Stage_H_F:   return "RV5Stage_H_F";
    default:                            return "none";
    }
}

/// A dialog plus handles on its controls, and a record of what it emitted.
struct Chooser
{
    ProcessorDialog   dialog;
    QRadioButton     *single     {nullptr};
    QRadioButton     *pipeline   {nullptr};
    QCheckBox        *hazard     {nullptr};
    QCheckBox        *forwarding {nullptr};
    QLabel           *note       {nullptr};
    QDialogButtonBox *buttons    {nullptr};

    ProcessorType emitted {ProcessorType::ProcessorTypeCount};
    int           fired   {0};

    explicit Chooser(ProcessorType current) : dialog(nullptr, current)
    {
        single     = dialog.findChild<QRadioButton *>("singleCycleRadio");
        pipeline   = dialog.findChild<QRadioButton *>("pipelineRadio");
        hazard     = dialog.findChild<QCheckBox *>("hazardCheck");
        forwarding = dialog.findChild<QCheckBox *>("forwardingCheck");
        note       = dialog.findChild<QLabel *>("noteLabel");
        buttons    = dialog.findChild<QDialogButtonBox *>("buttonBox");

        QObject::connect(&dialog, &ProcessorDialog::vmSelected, &dialog,
                         [this](const ProcessorType &type)
                         {
                             emitted = type;
                             ++fired;
                         });
    }

    bool complete() const
    {
        return single && pipeline && hazard && forwarding && note && buttons;
    }

    /// Drives the three controls the way a user would, then presses OK.
    void choose(bool wantPipeline, bool wantHazard, bool wantForwarding)
    {
        if (wantPipeline)
            pipeline->setChecked(true);
        else
            single->setChecked(true);
        hazard->setChecked(wantHazard);
        forwarding->setChecked(wantForwarding);
    }

    void pressOk()
    {
        buttons->button(QDialogButtonBox::Ok)->click();
    }
};
} // namespace

TEST(ProcessorChooserDialog, ItHasTheFourControlsAndANote)
{
    Chooser chooser(ProcessorType::RVSS);
    ASSERT_TRUE(chooser.complete())
        << "single=" << (chooser.single != nullptr) << " pipeline=" << (chooser.pipeline != nullptr)
        << " hazard=" << (chooser.hazard != nullptr)
        << " forwarding=" << (chooser.forwarding != nullptr)
        << " note=" << (chooser.note != nullptr) << " buttons=" << (chooser.buttons != nullptr);

    // There is no tree any more, and with it go the five literal rows and
    // the three that read "5 statge".
    EXPECT_EQ(chooser.dialog.findChildren<QRadioButton *>().size(), 2);
    EXPECT_EQ(chooser.dialog.findChildren<QCheckBox *>().size(), 2);
}

TEST(ProcessorChooserDialog, ItOpensShowingTheProcessorThatIsLoaded)
{
    for (ProcessorType type : allTypes())
    {
        Chooser chooser(type);
        ASSERT_TRUE(chooser.complete()) << nameOf(type);

        const ProcessorChoice want = processorChoiceFor(type);
        EXPECT_EQ(chooser.pipeline->isChecked(), want.pipeline) << nameOf(type) << ": Pipeline";
        EXPECT_EQ(chooser.single->isChecked(), !want.pipeline) << nameOf(type) << ": Single-cycle";
        EXPECT_EQ(chooser.hazard->isChecked(), want.hazard) << nameOf(type) << ": hazard box";
        EXPECT_EQ(chooser.forwarding->isChecked(), want.forwarding)
            << nameOf(type) << ": forwarding box";

        // And the boxes are live only for a pipeline.
        EXPECT_EQ(chooser.hazard->isEnabled(), want.pipeline) << nameOf(type) << ": hazard enabled";
        EXPECT_EQ(chooser.forwarding->isEnabled(), want.pipeline)
            << nameOf(type) << ": forwarding enabled";
    }
}

TEST(ProcessorChooserDialog, SingleCycleGreysTheBoxesWithoutClearingThem)
{
    // From every pipeline model: go to Single-cycle and the boxes must be
    // disabled and UNCHANGED, so the trip back is lossless.
    for (ProcessorType type : {ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_H_NF,
                               ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_H_F})
    {
        Chooser chooser(type);
        ASSERT_TRUE(chooser.complete()) << nameOf(type);

        const bool hazardWas     = chooser.hazard->isChecked();
        const bool forwardingWas = chooser.forwarding->isChecked();

        chooser.single->setChecked(true);
        EXPECT_FALSE(chooser.hazard->isEnabled()) << nameOf(type) << ": hazard still live";
        EXPECT_FALSE(chooser.forwarding->isEnabled()) << nameOf(type) << ": forwarding still live";
        EXPECT_EQ(chooser.hazard->isChecked(), hazardWas) << nameOf(type) << ": hazard was cleared";
        EXPECT_EQ(chooser.forwarding->isChecked(), forwardingWas)
            << nameOf(type) << ": forwarding was cleared";

        chooser.pipeline->setChecked(true);
        EXPECT_TRUE(chooser.hazard->isEnabled()) << nameOf(type) << ": hazard stayed grey";
        EXPECT_TRUE(chooser.forwarding->isEnabled()) << nameOf(type) << ": forwarding stayed grey";
        EXPECT_EQ(chooser.hazard->isChecked(), hazardWas) << nameOf(type) << ": hazard did not come back";
        EXPECT_EQ(chooser.forwarding->isChecked(), forwardingWas)
            << nameOf(type) << ": forwarding did not come back";
    }
}

TEST(ProcessorChooserDialog, TheRoundTripThroughSingleCycleKeepsTheModel)
{
    // The same property stated where it is felt: Pipeline -> Single ->
    // Pipeline, press OK, and the model is the one we started on.
    for (ProcessorType type : {ProcessorType::RV5Stage_NH_NF, ProcessorType::RV5Stage_H_NF,
                               ProcessorType::RV5Stage_NH_F, ProcessorType::RV5Stage_H_F})
    {
        Chooser chooser(type);
        ASSERT_TRUE(chooser.complete()) << nameOf(type);

        chooser.single->setChecked(true);
        chooser.pipeline->setChecked(true);
        chooser.pressOk();

        EXPECT_EQ(chooser.fired, 1) << nameOf(type);
        EXPECT_EQ(chooser.emitted, type)
            << nameOf(type) << " came back as " << nameOf(chooser.emitted);
    }
}

TEST(ProcessorChooserDialog, OkEmitsTheModelTheControlsSpellOut)
{
    struct Row
    {
        bool          pipeline;
        bool          hazard;
        bool          forwarding;
        ProcessorType type;
    };
    const Row rows[] = {
        {false, false, false, ProcessorType::RVSS},
        {false, false, true, ProcessorType::RVSS},
        {false, true, false, ProcessorType::RVSS},
        {false, true, true, ProcessorType::RVSS},
        {true, false, false, ProcessorType::RV5Stage_NH_NF},
        {true, true, false, ProcessorType::RV5Stage_H_NF},
        {true, false, true, ProcessorType::RV5Stage_NH_F},
        {true, true, true, ProcessorType::RV5Stage_H_F},
    };

    for (const Row &row : rows)
    {
        // Start somewhere else every time, so the emitted value cannot be
        // the preselect leaking through.
        Chooser chooser(row.type == ProcessorType::RVSS ? ProcessorType::RV5Stage_H_NF
                                                        : ProcessorType::RVSS);
        ASSERT_TRUE(chooser.complete());

        chooser.choose(row.pipeline, row.hazard, row.forwarding);
        chooser.pressOk();

        EXPECT_EQ(chooser.fired, 1) << "pipeline=" << row.pipeline << " hazard=" << row.hazard
                                    << " forwarding=" << row.forwarding;
        EXPECT_EQ(chooser.emitted, row.type)
            << "pipeline=" << row.pipeline << " hazard=" << row.hazard
            << " forwarding=" << row.forwarding << ": got " << nameOf(chooser.emitted)
            << ", wanted " << nameOf(row.type);
    }
}

TEST(ProcessorChooserDialog, CancelEmitsNothing)
{
    Chooser chooser(ProcessorType::RVSS);
    ASSERT_TRUE(chooser.complete());
    chooser.choose(true, true, true);
    chooser.buttons->button(QDialogButtonBox::Cancel)->click();
    EXPECT_EQ(chooser.fired, 0) << "a cancelled dialog changed the processor";
}

TEST(ProcessorChooserDialog, TheNoteFollowsTheControls)
{
    Chooser chooser(ProcessorType::RVSS);
    ASSERT_TRUE(chooser.complete());

    const struct
    {
        bool pipeline;
        bool hazard;
        bool forwarding;
    } states[] = {
        {false, true, true}, {true, true, true}, {true, true, false},
        {true, false, true}, {true, false, false},
    };

    for (const auto &state : states)
    {
        chooser.choose(state.pipeline, state.hazard, state.forwarding);
        EXPECT_EQ(chooser.note->text().toStdString(),
                  std::string(processorNote(state.pipeline, state.hazard, state.forwarding)))
            << "pipeline=" << state.pipeline << " hazard=" << state.hazard
            << " forwarding=" << state.forwarding;
        EXPECT_FALSE(chooser.note->text().isEmpty());
    }
}

TEST(ProcessorChooserDialog, TheNoteWarnsAboutNopsExactlyWhenHazardDetectionIsOff)
{
    Chooser chooser(ProcessorType::RVSS);
    ASSERT_TRUE(chooser.complete());

    // known_issues 77: on rv5-nh-nf AND rv5-nh-f a dependent chain comes
    // out as 0. Both no-hazard states must say so; neither hazard state
    // may, because both of those interlock.
    chooser.choose(true, false, false);
    EXPECT_NE(chooser.note->text().indexOf("nops"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, false, true);
    EXPECT_NE(chooser.note->text().indexOf("nops"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, true, false);
    EXPECT_EQ(chooser.note->text().indexOf("nops"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, true, true);
    EXPECT_EQ(chooser.note->text().indexOf("nops"), -1) << chooser.note->text().toStdString();
}

TEST(ProcessorChooserDialog, TheNoteMentionsWriteBackExactlyWhenForwardingIsOff)
{
    Chooser chooser(ProcessorType::RVSS);
    ASSERT_TRUE(chooser.complete());

    chooser.choose(true, true, false);
    EXPECT_NE(chooser.note->text().indexOf("write-back"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, false, false);
    EXPECT_NE(chooser.note->text().indexOf("write-back"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, true, true);
    EXPECT_EQ(chooser.note->text().indexOf("write-back"), -1) << chooser.note->text().toStdString();
    chooser.choose(true, false, true);
    EXPECT_EQ(chooser.note->text().indexOf("write-back"), -1) << chooser.note->text().toStdString();
}
