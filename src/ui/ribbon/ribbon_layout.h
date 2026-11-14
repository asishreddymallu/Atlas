#pragma once
#include <QList>

namespace Kites
{
/**
 * @brief What one ribbon group costs, in both modes.
 *
 * Widths are measured from the live widgets; nothing here knows how. A group
 * is overflowable only if everything it holds is a QToolButton, because an
 * overflowed group's commands are shown as QActions in a menu and a plain
 * widget - the execution-speed spin box - has no action to show and only one
 * parent to live under.
 */
struct RibbonGroupMetrics
{
    int  fullWidth    {0};
    int  compactWidth {0};
    bool overflowable {true};
};

/// Everything computeRibbonLayout() is allowed to look at.
struct RibbonLayoutInput
{
    /// Width the panel has to lay out in.
    int availableWidth {0};
    /// Run and File: always shown, never in the menu.
    QList<RibbonGroupMetrics> persistent;
    /// The current tab's groups, left to right.
    QList<RibbonGroupMetrics> pageGroups;
    /// What the overflow chevron costs, counted only when it is shown.
    int chevronWidth {0};
};

/// What the panel should look like. Index-aligned with pageGroups.
struct RibbonLayoutResult
{
    bool        compact {false};
    QList<bool> pageGroupInMenu;
    bool        chevronShown {false};

    bool operator==(const RibbonLayoutResult &other) const
    {
        return compact == other.compact && pageGroupInMenu == other.pageGroupInMenu &&
               chevronShown == other.chevronShown;
    }
    bool operator!=(const RibbonLayoutResult &other) const { return !(*this == other); }
};

/**
 * @brief Decide what the ribbon shows at a given width.
 *
 * A pure function of its argument. It never reads a widget, the current
 * geometry or the previous result, which is what makes it impossible for the
 * panel to oscillate: hiding a group narrows the panel, but the next call is
 * answered from the same inputs and returns the same answer.
 *
 * The degradation order, as the width shrinks:
 *
 *   1. everything full, no chevron
 *   2. everything compact, no chevron
 *   3. compact, chevron reserved, page groups into the menu RIGHTMOST FIRST
 *
 * There is no step for the persistent groups. Compact Run plus compact File
 * plus a chevron is 636 px, below the 849 px the content splitter needs on
 * its own, so a step that overflowed them could never be reached - the
 * window stops shrinking first.
 *
 * Step 3 is NOT theoretical. At the shipped widths it is reached on the two
 * busiest pages: Registers goes in the menu on Processor at 900 px and
 * below, Display on Memory at 849 px. See issue #11 for the sweep.
 *
 * The chevron costs width only once something is in the menu: steps 1 and 2
 * are tried without reserving it, and only if neither fits is it paid for.
 * Reserving it unconditionally would overflow a group at a width where
 * everything would otherwise have fitted.
 *
 * If the groups still do not fit once every overflowable one is in the menu,
 * the result simply says so. Nothing is clipped; the panel reports a larger
 * minimum width and the window refuses to shrink further.
 */
RibbonLayoutResult computeRibbonLayout(const RibbonLayoutInput &input);

/**
 * @brief The narrowest width at which the panel can still lay out.
 *
 * The persistent groups in compact mode, plus the chevron when there is a
 * page group that could need it. The page groups themselves contribute
 * nothing, because every one of them can go in the menu.
 */
int ribbonMinimumWidth(const RibbonLayoutInput &input);
} // namespace Kites
