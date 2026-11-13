#include "ribbon_layout.h"

namespace Kites
{
namespace
{
int widthOf(const RibbonGroupMetrics &group, bool compact)
{
    return compact ? group.compactWidth : group.fullWidth;
}

int totalOf(const QList<RibbonGroupMetrics> &groups, bool compact)
{
    int sum = 0;
    for (const RibbonGroupMetrics &group : groups)
        sum += widthOf(group, compact);
    return sum;
}

/// The page groups that are still on the panel, at `compact`.
int shownPageTotal(const QList<RibbonGroupMetrics> &groups, const QList<bool> &inMenu,
                   bool compact)
{
    int sum = 0;
    for (int i = 0; i < groups.size(); ++i)
        if (!inMenu.at(i))
            sum += widthOf(groups.at(i), compact);
    return sum;
}
} // namespace

RibbonLayoutResult computeRibbonLayout(const RibbonLayoutInput &input)
{
    RibbonLayoutResult result;
    result.pageGroupInMenu = QList<bool>(input.pageGroups.size(), false);

    const int persistentFull    = totalOf(input.persistent, false);
    const int persistentCompact = totalOf(input.persistent, true);

    // Step 1: everything as it wants to be, and no chevron to pay for.
    if (persistentFull + totalOf(input.pageGroups, false) <= input.availableWidth)
        return result;

    // Step 2: compact, still no chevron. Worth trying on its own - the
    // chevron must not be reserved at a width where compacting alone is
    // enough, or a group would overflow earlier than it needs to.
    result.compact = true;
    if (persistentCompact + totalOf(input.pageGroups, true) <= input.availableWidth)
        return result;

    // Step 3: pay for the chevron and drop page groups from the right until
    // what is left fits. Rightmost first keeps the group a page leads with
    // on the panel longest.
    const int budget = input.availableWidth - input.chevronWidth;
    for (int i = input.pageGroups.size() - 1; i >= 0; --i)
    {
        if (persistentCompact +
                shownPageTotal(input.pageGroups, result.pageGroupInMenu, true) <=
            budget)
            break;
        if (input.pageGroups.at(i).overflowable)
            result.pageGroupInMenu[i] = true;
    }

    for (bool inMenu : result.pageGroupInMenu)
        if (inMenu)
        {
            result.chevronShown = true;
            break;
        }
    return result;
}

int ribbonMinimumWidth(const RibbonLayoutInput &input)
{
    int minimum = totalOf(input.persistent, true);

    // A group that cannot go in the menu is always on the panel, so its
    // compact width is part of the floor.
    bool anyOverflowable = false;
    for (const RibbonGroupMetrics &group : input.pageGroups)
    {
        if (group.overflowable)
            anyOverflowable = true;
        else
            minimum += widthOf(group, true);
    }

    // The chevron is only part of the floor if there is something that could
    // end up behind it.
    if (anyOverflowable)
        minimum += input.chevronWidth;
    return minimum;
}
} // namespace Kites
