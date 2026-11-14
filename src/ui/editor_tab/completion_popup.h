#pragma once

#include "ui/editor_tab/completion_model.h"

#include <QList>
#include <QColor>
#include <QWidget>

namespace Kites
{

/**
 * @brief The suggestion list, as a child of the editor's viewport.
 *
 * Deliberately NOT QCompleter's popup. That popup is a top-level QListView:
 * it cannot be driven in kites_test without a platform that can raise a
 * window, it is outside the application stylesheet so it has no theme at all,
 * it takes the keyboard through an event filter installed on the editor, and
 * it gives no way to draw the matched characters, a kind badge or an elided
 * detail. Every rule this widget implements was reachable only by owning the
 * drawing - see known_issues for the full comparison.
 *
 * It takes no focus (Qt::NoFocus) and no mouse grab, so the editor keeps the
 * caret and every key still arrives at Editor::keyPressEvent, which decides
 * what the popup gets. That is what keeps the application's key sequences at
 * seven: there is no QAction and no QShortcut anywhere in this feature.
 */
class CompletionPopup : public QWidget
{
    Q_OBJECT
  public:
    explicit CompletionPopup(QWidget *parent = nullptr);

    /// Replaces the list. `matches` is already ranked; selection goes to the
    /// first item unless the previously selected label is still present, in
    /// which case it stays on it (VS Code keeps the selection while filtering).
    void setCandidates(const QList<Completion::Item> &items,
                       const QList<Completion::Match> &matches);

    bool isEmpty() const { return m_rows.isEmpty(); }
    int  count() const { return int(m_rows.size()); }
    int  currentRow() const { return m_current; }
    void setCurrentRow(int row);

    /// Up/Down wrap at the ends; a page is whatever is visible.
    void moveSelection(int delta);
    void movePage(int direction);

    QString selectedLabel() const;

    /**
     * @brief Places the popup under `caretLine`, left-aligned to `wordLeft`.
     *
     * Flips above the line when there is no room below, clamps inside the
     * viewport, and shrinks its height rather than hanging outside. All
     * coordinates are in the viewport's space.
     */
    void placeFor(const QRect &caretLine, int wordLeft, const QSize &viewportSize);

    /// True when the last placeFor() put it above the caret line.
    bool isAbove() const { return m_above; }

    /// The editor's font, so rows follow zoom exactly as the text does.
    void setEditorFont(const QFont &font);

    /// Row height at the current font, for the geometry tests.
    int rowHeight() const;

    /// How many rows are drawn at most before the list scrolls.
    static constexpr int maxVisibleRows = 12;

    /// The six colours the list draws with, for the contrast test. Derived
    /// from existing theme colours; no new token.
    struct Colours
    {
        QColor page, border, text, matched, detail, selected;
    };
    static Colours colours();

  signals:
    /// A row was clicked. The editor decides what inserting means.
    void rowActivated(const QString &label);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    struct Row
    {
        Completion::Item item;
        QList<int>       matched;
    };

    void ensureVisible();
    int  contentWidth() const;
    /// True when the detail column has been dropped because the popup is too
    /// narrow for it; the label is elided only after that.
    bool detailDropped() const { return m_detailDropped; }

    QList<Row> m_rows;
    int        m_current{0};
    int        m_top{0}; ///< first drawn row
    bool       m_above{false};
    bool       m_detailDropped{false};
};

} // namespace Kites
