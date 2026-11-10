#include "ribbon_group.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QVariant>

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

    // Insert before the trailing stretch so buttons stay left-aligned, and
    // centre it vertically. The alignment is not cosmetic: in a QBoxLayout a
    // child with a Fixed vertical policy also caps the row's maximum height,
    // so without it the 33 px spin box and the 37 px small buttons would
    // squash the 69 px large ones down to their own height. An aligned item
    // keeps its own height and stops constraining the row.
    m_row->insertWidget(m_row->count() - 1, button, 0, Qt::AlignVCenter);
    m_buttons.append(button);
    if (m_compact)
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    return button;
}

QToolButton *RibbonGroup::addLarge(QAction *action)
{
    return makeButton(action, Qt::ToolButtonTextUnderIcon);
}

QToolButton *RibbonGroup::addSmall(QAction *action)
{
    return makeButton(action, Qt::ToolButtonTextBesideIcon);
}

void RibbonGroup::addWidget(QWidget *widget)
{
    if (!widget)
        return;
    // Vertically centred for the same reason as the buttons: a Fixed-height
    // widget here would otherwise cap the whole row.
    m_row->insertWidget(m_row->count() - 1, widget, 0, Qt::AlignVCenter);
}

void RibbonGroup::addSeparator()
{
    auto *line = new QFrame(this);
    line->setObjectName("ribbonInnerSeparator");
    line->setFrameShape(QFrame::VLine);
    line->setFrameShadow(QFrame::Plain);
    line->setLineWidth(1);
    m_row->insertWidget(m_row->count() - 1, line);
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
            button->setToolButtonStyle(Qt::ToolButtonIconOnly);
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
} // namespace Kites
