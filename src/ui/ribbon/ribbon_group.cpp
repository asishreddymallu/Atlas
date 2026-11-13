#include "ribbon_group.h"

#include "ui/theme/icon_manager.h"

#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QLayoutItem>
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

// Icon sizes in logical pixels. The small size matches the toolbar and the
// tab strip; the large one is the Office-style icon-over-text button.
constexpr int smallIconSize = 20;
constexpr int largeIconSize = 32;

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
    probe.setIconSize(QSize(largeIconSize, largeIconSize));
    probe.setText(QStringLiteral("Wg"));
    // The icon has to be there, and non-null, or the probe measures a
    // text-only button and comes back at about half the real height. Its
    // appearance never matters - only that it occupies largeIconSize.
    QPixmap placeholder(largeIconSize, largeIconSize);
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
    probe.setIconSize(QSize(smallIconSize, smallIconSize));
    probe.setText(QStringLiteral("Wg"));
    QPixmap placeholder(smallIconSize, smallIconSize);
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

    auto *divider = new QFrame(this);
    m_divider     = divider;
    divider->setObjectName("ribbonGroupDivider");
    divider->setFrameShape(QFrame::VLine);
    divider->setFrameShadow(QFrame::Plain);
    divider->setLineWidth(1);

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
                            ? QSize(largeIconSize, largeIconSize)
                            : QSize(smallIconSize, smallIconSize));
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

    m_column->insertWidget(m_columnCount, widget);
    ++m_columnCount;
    m_columnSizes.last() = m_columnCount;
}

QToolButton *RibbonGroup::addLarge(QAction *action)
{
    QToolButton *button = makeButton(action, Qt::ToolButtonTextUnderIcon);
    addFullHeight(button);
    return button;
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
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setLineWidth(1);
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
            // A column host: as wide as its widest cell.
            for (int j = 0; j < inner->count(); ++j)
                if (QWidget *cell = inner->itemAt(j)->widget())
                    slotWidth = std::max(slotWidth, cell->sizeHint().width());
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

    const bool wasCompact  = m_compact;
    const bool wasUpdating = updatesEnabled();
    // Nothing is painted between here and the restore, so flipping the mode
    // twice cannot show.
    setUpdatesEnabled(false);

    setCompact(false);
    m_fullWidth = currentWidth();
    setCompact(true);
    m_compactWidth = currentWidth();

    setCompact(wasCompact);
    currentWidth(); // re-resolve the layout for the mode we are leaving in
    setUpdatesEnabled(wasUpdating);
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
