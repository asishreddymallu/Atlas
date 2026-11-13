/**
 * @file ribbon_layout_test.cpp
 * @brief The ribbon's overflow decision, tested without a single widget.
 *
 * computeRibbonLayout() is a pure function of its input, which is the whole
 * point: the panel cannot oscillate between two states, because hiding a
 * group changes the panel's width but not the function's answer.
 *
 * Every boundary below is derived from the metrics the test feeds in. None
 * is typed as a number, so changing a fixture width cannot leave a stale
 * literal passing for the wrong reason.
 */
#include "ui/ribbon/ribbon_layout.h"

#include <gtest/gtest.h>

#include <QList>

using namespace Kites;

namespace
{
/// Shapes roughly like the real ribbon: two persistent groups that never
/// overflow, and a page of three that may. The numbers are deliberately not
/// the application's - nothing here should depend on them.
RibbonLayoutInput fixture(int availableWidth)
{
    RibbonLayoutInput input;
    input.availableWidth = availableWidth;
    input.persistent     = {
        {500, 380, false}, // Run
        {260, 250, false}, // File
    };
    input.pageGroups = {
        {160, 150, true}, // leftmost
        {120, 110, true},
        {200, 190, true}, // rightmost, overflows first
    };
    input.chevronWidth = 30;
    return input;
}

int sumFull(const QList<RibbonGroupMetrics> &groups)
{
    int n = 0;
    for (const auto &g : groups)
        n += g.fullWidth;
    return n;
}

int sumCompact(const QList<RibbonGroupMetrics> &groups)
{
    int n = 0;
    for (const auto &g : groups)
        n += g.compactWidth;
    return n;
}

QList<int> menuIndices(const RibbonLayoutResult &result)
{
    QList<int> out;
    for (int i = 0; i < result.pageGroupInMenu.size(); ++i)
        if (result.pageGroupInMenu.at(i))
            out << i;
    return out;
}

RibbonLayoutResult at(int width)
{
    return computeRibbonLayout(fixture(width));
}
} // namespace

TEST(RibbonLayout, EverythingFitsAtFullWidth)
{
    const RibbonLayoutInput input = fixture(0);
    const int everything = sumFull(input.persistent) + sumFull(input.pageGroups);

    const RibbonLayoutResult wide = at(everything + 500);
    EXPECT_FALSE(wide.compact);
    EXPECT_FALSE(wide.chevronShown);
    EXPECT_TRUE(menuIndices(wide).isEmpty());

    // Exactly at the boundary it still fits; one pixel under, it does not.
    EXPECT_FALSE(at(everything).compact);
    EXPECT_TRUE(at(everything - 1).compact);
}

TEST(RibbonLayout, CompactComesBeforeTheMenu)
{
    const RibbonLayoutInput input = fixture(0);
    const int compactTotal = sumCompact(input.persistent) + sumCompact(input.pageGroups);

    // Anywhere between "compact just fits" and "full just fits", the answer
    // is compact with nothing hidden - and crucially no chevron, because
    // reserving it here would overflow a group that did not need to.
    const RibbonLayoutResult justCompact = at(compactTotal);
    EXPECT_TRUE(justCompact.compact);
    EXPECT_FALSE(justCompact.chevronShown);
    EXPECT_TRUE(menuIndices(justCompact).isEmpty());

    // One pixel narrower and the chevron is paid for and a group goes.
    const RibbonLayoutResult overflowing = at(compactTotal - 1);
    EXPECT_TRUE(overflowing.compact);
    EXPECT_TRUE(overflowing.chevronShown);
    EXPECT_FALSE(menuIndices(overflowing).isEmpty());
}

TEST(RibbonLayout, GroupsLeaveFromTheRight)
{
    const RibbonLayoutInput input = fixture(0);
    const int persistent = sumCompact(input.persistent);
    const auto &pages    = input.pageGroups;

    // Width at which exactly the last k groups are in the menu: the
    // persistent groups, the chevron, and the compact widths of the ones
    // still shown. Derived, never typed.
    auto widthWithLastKHidden = [&](int k)
    {
        int shown = 0;
        for (int i = 0; i < pages.size() - k; ++i)
            shown += pages.at(i).compactWidth;
        return persistent + input.chevronWidth + shown;
    };

    EXPECT_EQ(menuIndices(at(widthWithLastKHidden(1))), (QList<int>{2}));
    EXPECT_EQ(menuIndices(at(widthWithLastKHidden(2))), (QList<int>{1, 2}));
    EXPECT_EQ(menuIndices(at(widthWithLastKHidden(3))), (QList<int>{0, 1, 2}));

    // Narrower than everything hidden: still all three, nothing worse.
    EXPECT_EQ(menuIndices(at(0)), (QList<int>{0, 1, 2}));
}

TEST(RibbonLayout, NonOverflowableGroupsNeverEnterTheMenu)
{
    RibbonLayoutInput input      = fixture(0);
    input.pageGroups[1].overflowable = false; // a page group holding a widget

    for (int width = 0; width <= 2000; ++width)
    {
        input.availableWidth = width;
        const RibbonLayoutResult result = computeRibbonLayout(input);
        EXPECT_FALSE(result.pageGroupInMenu.at(1))
            << "a group that cannot be shown as actions went into the menu at " << width;
    }

    // ...and the persistent groups are not representable in the menu at all,
    // so the result has no slot for them: the only list is the page groups'.
    input.availableWidth = 0;
    EXPECT_EQ(computeRibbonLayout(input).pageGroupInMenu.size(), input.pageGroups.size());
}

TEST(RibbonLayout, SweepIsMonotonicAndConsistent)
{
    RibbonLayoutResult previous;   // the result at the width one pixel wider
    bool               havePrevious = false;

    const RibbonLayoutInput probe = fixture(0);
    const int persistentCompact   = sumCompact(probe.persistent);

    // Sweeping downwards, so "previous" is always the wider window.
    for (int width = 2000; width >= 0; --width)
    {
        const RibbonLayoutInput  input  = fixture(width);
        const RibbonLayoutResult result = computeRibbonLayout(input);

        // Pure: the same input twice gives the same answer, and a freshly
        // built input gives the same answer as the one just used.
        EXPECT_TRUE(result == computeRibbonLayout(input)) << "not deterministic at " << width;
        EXPECT_TRUE(result == at(width)) << "depends on something outside its input at " << width;

        // A group is shown or in the menu, never both and never neither.
        // The lists are index-aligned, so size is the whole of that claim.
        ASSERT_EQ(result.pageGroupInMenu.size(), input.pageGroups.size());

        // The chevron is shown exactly when something is behind it.
        EXPECT_EQ(result.chevronShown, !menuIndices(result).isEmpty())
            << "chevron and menu disagree at " << width;

        // Whatever is shown fits, as long as the floor itself fits.
        if (persistentCompact + input.chevronWidth <= width)
        {
            int used = persistentCompact;
            for (int i = 0; i < input.pageGroups.size(); ++i)
                if (!result.pageGroupInMenu.at(i))
                    used += result.compact ? input.pageGroups.at(i).compactWidth
                                           : input.pageGroups.at(i).fullWidth;
            if (result.chevronShown)
                used += input.chevronWidth;
            EXPECT_LE(used, width) << "the shown groups do not fit at " << width;
        }

        if (havePrevious)
        {
            // Compact never switches back off as the window narrows.
            EXPECT_TRUE(!previous.compact || result.compact)
                << "compact turned off going from " << width + 1 << " to " << width;
            // No group ever comes back out of the menu.
            for (int i = 0; i < result.pageGroupInMenu.size(); ++i)
                EXPECT_TRUE(!previous.pageGroupInMenu.at(i) || result.pageGroupInMenu.at(i))
                    << "group " << i << " left the menu going from " << width + 1 << " to "
                    << width;
        }
        previous     = result;
        havePrevious = true;
    }
}

TEST(RibbonLayout, MinimumWidthIsThePersistentGroupsAndTheChevron)
{
    RibbonLayoutInput input = fixture(0);
    EXPECT_EQ(ribbonMinimumWidth(input), sumCompact(input.persistent) + input.chevronWidth);

    // The page groups contribute nothing, because all of them can go in the
    // menu - which is what stops a crowded page setting the floor for every
    // tab.
    RibbonLayoutInput empty = input;
    empty.pageGroups.clear();
    EXPECT_EQ(ribbonMinimumWidth(empty), sumCompact(input.persistent));

    // A page group that cannot overflow does contribute.
    RibbonLayoutInput pinned          = input;
    pinned.pageGroups[1].overflowable = false;
    EXPECT_EQ(ribbonMinimumWidth(pinned), sumCompact(input.persistent) + input.chevronWidth +
                                              input.pageGroups.at(1).compactWidth);

    // At the minimum the layout still produces an answer: everything that
    // can be in the menu is, and nothing is clipped.
    const RibbonLayoutResult atFloor = at(ribbonMinimumWidth(input));
    EXPECT_TRUE(atFloor.compact);
    EXPECT_EQ(menuIndices(atFloor), (QList<int>{0, 1, 2}));
}

TEST(RibbonLayout, EmptyPageNeedsNoChevron)
{
    // The Profiler page has no groups of its own.
    RibbonLayoutInput input = fixture(0);
    input.pageGroups.clear();

    for (int width = 0; width <= 2000; ++width)
    {
        input.availableWidth = width;
        const RibbonLayoutResult result = computeRibbonLayout(input);
        EXPECT_FALSE(result.chevronShown) << "a chevron with nothing behind it at " << width;
        EXPECT_TRUE(result.pageGroupInMenu.isEmpty());
    }
}
