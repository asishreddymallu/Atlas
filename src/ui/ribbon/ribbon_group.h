#pragma once
#include <QList>
#include <QString>
#include <QWidget>

class QAction;
class QHBoxLayout;
class QVBoxLayout;
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

    /// Icon above text, full content height: for the one or two commands a
    /// group leads with. Ends any column of small items in progress.
    QToolButton *addLarge(QAction *action);

    /// Icon beside text, one cell high. Small items stack in COLUMNS the way
    /// an Office ribbon does: each one goes under the previous until the
    /// column is full, then a new column starts to its right.
    QToolButton *addSmall(QAction *action);

    /// A plain widget (a spin box, say). Stacks with the small buttons, in
    /// the same column grid, and is sized to the same cell.
    void addWidget(QWidget *widget);

    /// A hairline divider inside the group, for sub-blocks of related
    /// buttons. Full height, and it ends the current column - which is what
    /// makes the Run group read as three pairs rather than one long column.
    void addSeparator();

    /// How many small items fit in one column, and how tall each cell is.
    /// Both are derived at runtime from the content height a large button
    /// asks for and from the font, so a different font size or DPI gets a
    /// different answer rather than a clipped one. Nothing is hard-coded.
    int rowCapacity() const;
    int cellHeight() const;

    /// Columns built so far, left to right, as the number of items in each.
    /// Exposed for the layout tests.
    QList<int> columnSizes() const;

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
    /// Appends to the column in progress, starting a new one when it is full.
    void addToColumn(QWidget *widget);
    /// Ends the column in progress, so the next small item starts a new one.
    void endColumn();
    /// Appends a full-height item (a large button or a separator).
    void addFullHeight(QWidget *widget);

    QHBoxLayout         *m_row     {nullptr};
    QLabel              *m_caption {nullptr};
    QList<QToolButton *> m_buttons;
    bool                 m_compact {false};

    // The column being filled, and how many items are in it. Null between
    // columns - after a large button, after a separator, and at the start.
    QVBoxLayout         *m_column      {nullptr};
    int                  m_columnCount {0};
    QList<int>           m_columnSizes;
};
} // namespace Kites
