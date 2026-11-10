#pragma once
#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace Kites
{
/**
 * @brief One labelled block of commands on a ribbon page.
 *
 * Lays its controls out in a single row with a small muted caption
 * underneath, the way an Office ribbon group reads. A thin separator to the
 * right of the row divides it from the next group.
 *
 * The group owns no command logic. Every button is driven by a QAction the
 * caller supplies, through QToolButton::setDefaultAction(), so text, icon,
 * tooltip, enabled and checked state all stay with that action and are
 * mirrored by Qt in both directions. Putting the same action on a ribbon
 * button and on a menu or toolbar therefore creates no second copy of its
 * state and no second keyboard shortcut.
 */
class RibbonGroup : public QWidget
{
    Q_OBJECT

  public:
    explicit RibbonGroup(const QString &caption, QWidget *parent = nullptr);

    /// Icon above text: for the one or two commands a group leads with.
    QToolButton *addLarge(QAction *action);
    /// Icon beside text: for everything else.
    QToolButton *addSmall(QAction *action);
    /// A plain widget (a spin box, say) placed in the row between buttons.
    void addWidget(QWidget *widget);
    /// A hairline divider inside the group, for sub-blocks of related buttons.
    void addSeparator();

    QString caption() const;
    /// Buttons added so far, in the order they were added.
    QList<QToolButton *> buttons() const;

    /**
     * @brief Icon-only mode for narrow windows.
     *
     * Hides button text and keeps the caption, so a group still says what it
     * is when its labels are gone. The visual treatment lands in Stage 5;
     * this is the switch that stage drives.
     */
    void setCompact(bool compact);
    bool isCompact() const;

  private:
    QToolButton *makeButton(QAction *action, Qt::ToolButtonStyle style);

    QHBoxLayout         *m_row     {nullptr};
    QLabel              *m_caption {nullptr};
    QList<QToolButton *> m_buttons;
    bool                 m_compact {false};
};
} // namespace Kites
