#pragma once

#include "ui/editor_tab/label_index.h"

#include <QColor>
#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;

namespace Atlas
{

/**
 * @brief The Shift+F12 references list.
 *
 * A child of the editor's viewport, in no layout of its own beyond a simple
 * column - the same conventions as FindBar and GotoLineBar, including the
 * one-overlay-at-a-time rule and the painted background. A plain QWidget
 * draws neither the background nor the border a stylesheet gives it, so
 * this paints its own; FindBar shipped see-through for exactly that reason.
 */
class ReferencesBar : public QWidget
{
    Q_OBJECT
  public:
    explicit ReferencesBar(QWidget *parent = nullptr);

    struct Row
    {
        int     line{0};   ///< 0-based
        QString text;      ///< the line's text, trimmed
        bool    isDefinition{false};
        int     offset{0}; ///< where to jump to
    };

    /// Fills the list. An empty list is refused by the caller, not here.
    void setRows(const QList<Row> &rows, const QString &name);
    int  count() const { return int(m_rows.size()); }
    int  currentRow() const { return m_current; }
    /// Selects a row outright. For the tests, which read each row's line.
    void setCurrentRow(int row);
    void moveSelection(int delta);
    /// The row the user is on, or -1.
    const Row *selected() const;

    /**
     * @brief Keeps it in the top-right of the viewport and inside it.
     *
     * `topOffset` is how far down to start. The find bar occupies the same
     * corner and is NOT closed when this opens - a reader mid-search keeps
     * their search - so the list is placed under it rather than over it.
     */
    void reposition(const QWidget *viewport, int topOffset = 0);
    /// The editor's font, so the list follows zoom as the text does.
    void setEditorFont(const QFont &font);
    int  rowHeight() const;

    /// True when the key was one of the bar's own.
    bool handleKey(QKeyEvent *event);

    static constexpr int maxVisibleRows = 10;

    /// The five colours the list draws with, for the contrast test. Derived
    /// from existing theme colours; no new token. Exposed for the same
    /// reason CompletionPopup::colours() is: the harness must measure what
    /// is painted, not a second copy of the derivation.
    struct Colours
    {
        QColor page, border, text, dim, selected;
    };
    static Colours colours();

  signals:
    void picked(int offset);
    void closeRequested();

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    void ensureVisible();

    QList<Row> m_rows;
    QString    m_name;
    int        m_current{0};
    int        m_top{0};
};

/**
 * @brief The F2 rename overlay.
 *
 * A single line edit prefilled with the name, plus a reason line that only
 * appears when the new name is refused. Enter applies, Esc cancels.
 */
class RenameBar : public QWidget
{
    Q_OBJECT
  public:
    explicit RenameBar(QWidget *parent = nullptr);

    /// Opens it on `name`, with the text selected so typing replaces it.
    void openOn(const QString &name);
    QString newName() const;
    /// The visible refusal, or empty when none is shown. For the tests.
    QString reasonText() const;
    /// Shows why a name was refused, and keeps the bar open.
    void showReason(const QString &reason);
    void clearReason();

    /// `topOffset` is how far down to start, so an open find bar - which
    /// owns the same corner and is deliberately NOT closed - is not covered.
    void reposition(const QWidget *viewport, int topOffset = 0);
    void setEditorFont(const QFont &font);

    /// The input, so a test can type into it without a focus policy fight.
    QLineEdit *input() const { return m_input; }
    /// Sets the field and keeps the overlay inside the viewport. A long
    /// name set straight on input() does not re-clamp; this does.
    void setName(const QString &name);

  signals:
    void accepted();
    void closeRequested();

  protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    /// Re-measures and clips the overlay to its parent viewport.
    void clampInsideParent();

    QLineEdit *m_input{nullptr};
    QLabel    *m_reason{nullptr};
};

/**
 * @brief Checks a proposed rename, with the reason when it is refused.
 *
 * Pure: the rules come from the assembler (read, never modified) and from
 * the index. Empty `reason` means the name is acceptable.
 */
struct RenameCheck
{
    bool    ok{false};
    QString reason;
};
RenameCheck checkRename(const QString &oldName, const QString &newName,
                        const LabelIndex::Index &index);

} // namespace Atlas
