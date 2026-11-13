#include "ribbon_group.h"

#include "ui/theme/icon_manager.h"

#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QLayoutItem>
#include <QSizePolicy>
#include <QFrame>
#include <QIcon>
#include <QPixmap>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>
#include <algorithm>

namespace Kites
{
namespace
{
// Dynamic property remembering the style a button was created with, so
// leaving compact mode restores exactly what the caller asked for instead of
// guessing from the button's current shape.
constexpr const char *restingStyleProperty = "ribbonRestingStyle";


// Vertical gap between stacked small items, and the most that are ever put in
// one column. Three is the Office shape; two and one are what a larger font
// or a lower content height fall back to.
constexpr int columnSpacing = 2;
constexpr int maxRowsPerColumn = 3;

// Marks a button as stacked, so template.qss can take its vertical padding
// down to a hairline. Without that a column of three would need 115 px in a
// 69 px band.
constexpr const char *stackedProperty = "ribbonStacked";

// The height one row of controls occupies, measured rather than assumed: it
// is whatever a large icon-over-text button asks for under the current style,
// font and device pixel ratio. The probe is built and thrown away; it is the
// same question the real large buttons answer when they are laid out.
int contentHeight()
{
    QToolButton probe;
    probe.setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    probe.setIconSize(QSize(largeRibbonIconSize, largeRibbonIconSize));
    probe.setText(QStringLiteral("Wg"));
    // The icon has to be there, and non-null, or the probe measures a
    // text-only button and comes back at about half the real height. Its
    // appearance never matters - only that it occupies largeRibbonIconSize.
    QPixmap placeholder(largeRibbonIconSize, largeRibbonIconSize);
    placeholder.fill(Qt::transparent);
    probe.setIcon(QIcon(placeholder));
    return probe.sizeHint().height();
}

// How tall one stacked small item actually is under the current style, font
// and device pixel ratio. Measured the same way contentHeight() is: a probe
// carrying the same property, so the same stylesheet rule applies to it.
int stackedItemHeight()
{
    QToolButton probe;
    probe.setProperty(stackedProperty, true);
    probe.setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    probe.setIconSize(QSize(smallRibbonIconSize, smallRibbonIconSize));
    probe.setText(QStringLiteral("Wg"));
    QPixmap placeholder(smallRibbonIconSize, smallRibbonIconSize);
    placeholder.fill(Qt::transparent);
    probe.setIcon(QIcon(placeholder));
    return probe.sizeHint().height();
}

// Compact mode hides button text. A button with no icon has nothing left to
// show, so it keeps its label instead of becoming a blank square. The View
// group's Editor/Expanded pair is the case in point: the in-tab control it
// mirrors is text-only too.
//
// The question is asked of the action's icon *tag*, not of the pixmap. An
// action tagged by setThemedIcon() is meant to have an icon whether or not
// the resource resolved, which keeps this independent of whether the .qrc
// objects are linked - they are in the application and are not in
// kites_test. A directly-set QIcon counts too.
Qt::ToolButtonStyle compactStyleFor(const QToolButton *button)
{
    const QAction *action = button->defaultAction();
    const bool     hasIcon =
        (action && themedIconOf(action) != Icon::IconCount) || !button->icon().isNull();
    return hasIcon ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly;
}
} // namespace

RibbonGroup::RibbonGroup(const QString &caption, QWidget *parent) : QWidget(parent)
{
    // [ content column ][ divider ] - the divider is a child of the group so
    // it spans only this group's height, the way an Office group rule does.
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);

    auto *content = new QWidget(this);
    auto *column  = new QVBoxLayout(content);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(2);

    // The row holds SLOTS, not individual controls: a large button, a
    // separator, or a column of up to rowCapacity() small items. That is what
    // turns a group from one long line into an Office-shaped block.
    auto *rowHost = new QWidget(content);
    m_row = new QHBoxLayout(rowHost);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(2);
    m_row->addStretch(0);

    m_caption = new QLabel(caption, content);
    m_caption->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    // Stage 5 styles the caption through this property; it carries no colour
    // of its own, so the theme stays the single source of colour.
    m_caption->setProperty("ribbonCaption", true);

    column->addWidget(rowHost, 1);
    column->addWidget(m_caption, 0);

    // A plain styled widget, not a QFrame::VLine. A VLine takes its colour
    // from the palette, which gave a hard near-white rule the theme had no
    // say over; this one is a 1 px background in the border token, and the
    // stylesheet can inset it top and bottom.
    auto *divider = new QFrame(this);
    m_divider     = divider;
    divider->setObjectName("ribbonGroupDivider");
    divider->setFrameShape(QFrame::NoFrame);
    divider->setAttribute(Qt::WA_StyledBackground, true);
    divider->setFixedWidth(1);

    outer->addWidget(content, 1);
    outer->addWidget(divider, 0);

    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
}

QToolButton *RibbonGroup::makeButton(QAction *action, Qt::ToolButtonStyle style)
{
    auto *button = new QToolButton(this);
    button->setDefaultAction(action);
    button->setToolButtonStyle(style);
    button->setProperty(restingStyleProperty, static_cast<int>(style));
    button->setIconSize(style == Qt::ToolButtonTextUnderIcon
                            ? QSize(largeRibbonIconSize, largeRibbonIconSize)
                            : QSize(smallRibbonIconSize, smallRibbonIconSize));
    button->setAutoRaise(true);
    // The ribbon is reached with the mouse, the menus or a shortcut. Keeping
    // the buttons out of the tab chain stops Tab from walking a dozen chrome
    // controls before it reaches the editor; the tab strip is the one chrome
    // stop. Recorded as a known limitation in docs/audit/known_issues.md.
    button->setFocusPolicy(Qt::NoFocus);

    m_buttons.append(button);
    invalidateWidths();
    if (m_compact)
        button->setToolButtonStyle(compactStyleFor(button));
    return button;
}

int RibbonGroup::rowCapacity() const
{
    const int cell = stackedItemHeight();
    if (cell <= 0)
        return 1;
    // How many whole cells, with their gaps, fit in the band a large button
    // defines. Both numbers are measured; neither is a literal, so a bigger
    // font or a different style gets a smaller answer rather than a clipped
    // column.
    const int fits = (contentHeight() + columnSpacing) / (cell + columnSpacing);
    return std::clamp(fits, 1, maxRowsPerColumn);
}

int RibbonGroup::cellHeight() const
{
    return stackedItemHeight();
}

QList<int> RibbonGroup::columnSizes() const
{
    return m_columnSizes;
}

void RibbonGroup::endColumn()
{
    m_column      = nullptr;
    m_columnCount = 0;
}

void RibbonGroup::addFullHeight(QWidget *widget)
{
    endColumn();
    // Before the trailing stretch, so slots stay left-aligned, and centred
    // vertically. The alignment is not cosmetic: in a QBoxLayout a child with
    // a Fixed vertical policy also caps the row's maximum height, so without
    // it a short slot would squash the large buttons down to its own height.
    m_row->insertWidget(m_row->count() - 1, widget, 0, Qt::AlignVCenter);
}

void RibbonGroup::addToColumn(QWidget *widget)
{
    const int rows = rowCapacity();
    const int cell = cellHeight();

    if (!m_column || m_columnCount >= rows)
    {
        Q_UNUSED(cell);
        auto *host = new QWidget(this);
        m_column   = new QVBoxLayout(host);
        m_column->setContentsMargins(0, 0, 0, 0);
        m_column->setSpacing(columnSpacing);
        m_column->addStretch(0); // partial columns sit at the top, like Office
        m_columnCount = 0;
        m_columnSizes.append(0);
        // The column is as tall as its contents; AlignVCenter keeps it from
        // capping the row the way a Fixed-height child would.
        m_row->insertWidget(m_row->count() - 1, host, 0, Qt::AlignVCenter);
    }

    // Marked before it is laid out, so the stylesheet's reduced padding is in
    // the size hint the layout reads. Nothing forces a height here: the
    // buttons take the height the style gives them, and rowCapacity() was
    // derived from that same measurement, so the column cannot overflow the
    // band. Forcing one would fight the stylesheet and clip the icons.
    widget->setProperty(stackedProperty, true);

    // Everything in a column takes the column's width, which is the widest
    // item's. A QToolButton is Minimum horizontally by default and would
    // otherwise shrink to its own label, leaving a ragged right edge - and
    // leaving the speed spin box narrower than the Processor button above
    // it. The column host is only as wide as its widest child, so this
    // cannot make the group greedy.
    QSizePolicy policy = widget->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::MinimumExpanding);
    widget->setSizePolicy(policy);

    m_column->insertWidget(m_columnCount, widget);
    ++m_columnCount;
    m_columnSizes.last() = m_columnCount;
}

int RibbonGroup::largeMinimumWidth() const
{
    // The icon, plus six characters' worth of label either side of it. Six
    // 'n's is the usual stand-in for a readable minimum; at the current font
    // it comes to about the width of "Debug Run" minus its longest word, so
    // Run and Open stop looking like half-buttons next to it.
    return largeRibbonIconSize + 6 * fontMetrics().horizontalAdvance(QLatin1Char('n'));
}

QToolButton *RibbonGroup::addLarge(QAction *action)
{
    QToolButton *button = makeButton(action, Qt::ToolButtonTextUnderIcon);
    // A minimum, not a fixed width: a longer label still gets the room it
    // needs, and every large button starts from the same floor.
    button->setMinimumWidth(largeMinimumWidth());
    addFullHeight(button);
    return button;
}

QList<QToolButton *> RibbonGroup::addSegmented(const QList<QAction *> &actions)
{
    QList<QToolButton *> buttons;
    if (actions.isEmpty())
        return buttons;

    endColumn();

    auto *host = new QWidget(this);
    auto *row  = new QHBoxLayout(host);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0); // the segments touch; their shared borders are removed

    for (int i = 0; i < actions.size(); ++i)
    {
        QToolButton *button = makeButton(actions.at(i), Qt::ToolButtonTextBesideIcon);
        const char  *where  = actions.size() == 1 ? "only"
                              : i == 0            ? "first"
                              : i == actions.size() - 1 ? "last"
                                                        : "middle";
        button->setProperty("ribbonSegment", QLatin1String(where));
        // Same height rule as a stacked button, so a segmented run lines up
        // with the columns beside it.
        button->setProperty(stackedProperty, true);
        row->addWidget(button);
        buttons.append(button);
    }

    m_row->insertWidget(m_row->count() - 1, host, 0, Qt::AlignVCenter);
    return buttons;
}

QToolButton *RibbonGroup::addSmall(QAction *action)
{
    QToolButton *button = makeButton(action, Qt::ToolButtonTextBesideIcon);
    addToColumn(button);
    return button;
}

void RibbonGroup::addWidget(QWidget *widget)
{
    if (!widget)
        return;
    // A menu entry is a QAction and this is not one, so from here the group
    // can never be moved into the overflow menu. It is also the reason the
    // Run group is pinned: a widget has exactly one parent, and this is the
    // one speed spin box.
    m_hasPlainWidget = true;
    // Reparented into the column, not copied.
    addToColumn(widget);
}

void RibbonGroup::addSeparator()
{
    auto *line = new QFrame(this);
    line->setObjectName("ribbonInnerSeparator");
    line->setFrameShape(QFrame::NoFrame);
    line->setAttribute(Qt::WA_StyledBackground, true);
    line->setFixedWidth(1);
    addFullHeight(line);
}

QString RibbonGroup::caption() const
{
    return m_caption->text();
}

QList<QToolButton *> RibbonGroup::buttons() const
{
    return m_buttons;
}

void RibbonGroup::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;

    for (QToolButton *button : std::as_const(m_buttons))
    {
        if (compact)
        {
            button->setToolButtonStyle(compactStyleFor(button));
            continue;
        }
        bool ok = false;
        const int resting = button->property(restingStyleProperty).toInt(&ok);
        button->setToolButtonStyle(ok ? static_cast<Qt::ToolButtonStyle>(resting)
                                      : Qt::ToolButtonTextBesideIcon);
    }
}

bool RibbonGroup::isCompact() const
{
    return m_compact;
}

int RibbonGroup::currentWidth()
{
    // Summed from the slots rather than read from sizeHint().
    //
    // Inside a window that is already shown, updateGeometry() only POSTS a
    // layout request, so a sizeHint() read in the same breath still answers
    // from before the change - a group whose button had just grown reported
    // its old width, and the overflow decision was made on stale numbers.
    // Forcing the layout with activate() does not reach the nested column
    // layouts either.
    //
    // Every leaf widget's own sizeHint() is current, always, so the sum is.
    // The margins and spacings are read from the layouts rather than
    // written down, so this stays in step with the constructor.
    int row   = 0;
    int slotCount = 0;
    for (int i = 0; i < m_row->count(); ++i)
    {
        QWidget *slot = m_row->itemAt(i)->widget();
        if (!slot) // the trailing stretch
            continue;

        int slotWidth = 0;
        if (QLayout *inner = slot->layout())
        {
            // Two kinds of slot carry a layout and they measure differently.
            // A COLUMN stacks its cells, so it is as wide as the widest one;
            // a SEGMENTED RUN sits side by side, so it is as wide as all of
            // them put together. Treating the second like the first reports
            // about a third of its real width, the layout function then
            // believes it fits, and the panel squeezes the labels away to
            // nothing rather than moving the group into the overflow menu.
            const bool sideBySide = qobject_cast<QHBoxLayout *>(inner) != nullptr;
            int        cells      = 0;
            for (int j = 0; j < inner->count(); ++j)
            {
                QWidget *cell = inner->itemAt(j)->widget();
                if (!cell)
                    continue;
                const int cellWidth = cell->sizeHint().width();
                if (sideBySide)
                {
                    slotWidth += cellWidth;
                    ++cells;
                }
                else
                {
                    slotWidth = std::max(slotWidth, cellWidth);
                }
            }
            if (sideBySide && cells > 1)
                slotWidth += inner->spacing() * (cells - 1);
        }
        else
        {
            // A large button or a separator.
            slotWidth = slot->sizeHint().width();
        }

        if (slotWidth <= 0)
            continue;
        row += slotWidth;
        ++slotCount;
    }
    if (slotCount > 1)
        row += m_row->spacing() * (slotCount - 1);

    // The caption sits under the row and can be the wider of the two.
    int content = std::max(row, m_caption->sizeHint().width());

    if (QLayout *column = m_caption->parentWidget()->layout())
    {
        const QMargins margins = column->contentsMargins();
        content += margins.left() + margins.right();
    }
    if (QLayout *own = layout())
    {
        const QMargins margins = own->contentsMargins();
        content += margins.left() + margins.right() + own->spacing();
    }
    if (m_divider)
        content += m_divider->sizeHint().width();
    return content;
}

void RibbonGroup::measure()
{
    if (m_fullWidth >= 0 && m_compactWidth >= 0)
        return;
    // Re-entrant: setCompact() below changes button styles, which asks the
    // parent chain for a new size, which asks this group for its widths
    // again. Without the guard the inner call runs on half-set state.
    if (m_measuring)
        return;
    m_measuring = true;

    const bool wasCompact = m_compact;

    setCompact(false);
    m_fullWidth = currentWidth();
    setCompact(true);
    m_compactWidth = currentWidth();

    setCompact(wasCompact);

    // Deliberately NOT wrapped in setUpdatesEnabled(false)/(true).
    //
    // It was, and it is how the page groups went invisible: a group is
    // measured while its page is not the current one, the re-entrant call
    // above captured updatesEnabled() as already false, and the restore put
    // false back for good. The whole flip is synchronous and Qt coalesces
    // the repaint requests it raises, so the intermediate mode is never
    // painted and there is nothing to suppress.
    m_measuring = false;
}

int RibbonGroup::fullWidth()
{
    measure();
    return m_fullWidth;
}

int RibbonGroup::compactWidth()
{
    measure();
    return m_compactWidth;
}

void RibbonGroup::invalidateWidths()
{
    m_fullWidth    = -1;
    m_compactWidth = -1;
}

bool RibbonGroup::isOverflowable() const
{
    return !m_hasPlainWidget;
}

void RibbonGroup::changeEvent(QEvent *event)
{
    switch (event->type())
    {
        // A theme reapplies the application stylesheet, which reaches every
        // widget as a StyleChange. Both of the font cases matter because the
        // cells are sized from font metrics.
        case QEvent::StyleChange:
        case QEvent::FontChange:
        case QEvent::ApplicationFontChange:
        case QEvent::LayoutDirectionChange:
            invalidateWidths();
            break;
        default:
            break;
    }
    QWidget::changeEvent(event);
}
} // namespace Kites
