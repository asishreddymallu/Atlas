#pragma once
#include "ui/editor_tab/find_engine.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QToolButton;
class QWidget;

namespace Kites
{
/**
 * @brief The find (and replace) overlay for an editor pane.
 *
 * An OVERLAY, parented to the editor's viewport and positioned by hand. It
 * is deliberately not in any layout: the viewport is exactly the same size
 * whether the bar is open or closed, so opening it never reflows the text
 * or moves the gutters. A test asserts that.
 *
 * The bar owns no search logic. It collects a pattern and some options,
 * says so, and displays whatever status it is given; FindEngine does the
 * finding and Editor applies the result.
 *
 * None of its keys is a QShortcut or a QAction. Alt+C, Alt+W and Alt+R are
 * handled here, in the bar, because in the editor Alt+letter inserts that
 * letter - measured - and that behaviour is not this feature's to change.
 */
class FindBar : public QWidget
{
    Q_OBJECT

  public:
    explicit FindBar(QWidget *parent = nullptr);

    FindEngine::FindOptions options() const;
    QString                 pattern() const;
    QString                 replacement() const;

    void setPattern(const QString &pattern);
    /// Focuses the find field and selects what is in it.
    void focusFind();
    void focusReplace();

    /// The replace row is hidden until Ctrl+H asks for it.
    void setReplaceVisible(bool visible);
    bool isReplaceVisible() const;

    /// "3 of 12", "No results", "Invalid regex: ...", "Replaced 7".
    void setStatus(const QString &text, bool isError = false);
    QString status() const;

    /// Replace is refused in a read-only pane, so the row's controls go
    /// insensitive rather than silently doing nothing.
    void setReplaceEnabled(bool enabled);

    /// Keeps the bar in the top-right of `viewport`, never wider than it
    /// minus the margin on each side.
    void reposition(const QWidget *viewport);

  signals:
    void patternChanged();
    void optionsChanged();
    void findNext();
    void findPrevious();
    void replaceOne();
    void replaceAll();
    void closeRequested();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    /// True when the key was one of the bar's own.
    bool handleBarKey(QKeyEvent *event);

    QLineEdit   *m_find        {nullptr};
    QLineEdit   *m_replace     {nullptr};
    QWidget     *m_replaceRow  {nullptr};
    QToolButton *m_caseButton  {nullptr};
    QToolButton *m_wordButton  {nullptr};
    QToolButton *m_regexButton {nullptr};
    QLabel      *m_status      {nullptr};
    QToolButton *m_replaceOne  {nullptr};
    QToolButton *m_replaceAll  {nullptr};
};
} // namespace Kites
