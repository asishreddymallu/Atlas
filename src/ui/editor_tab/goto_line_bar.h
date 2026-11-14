#pragma once
#include <QWidget>

class QLineEdit;

namespace Kites
{
/**
 * @brief The "go to line" overlay.
 *
 * Same shape as the find bar - a child of the editor's viewport, positioned
 * by hand, in no layout, so the viewport is the same size whether it is
 * open or not. Only one of the two is ever visible at a time; the editor
 * closes the other before showing one.
 *
 * It accepts `12` and `12:5`. A number past the end goes to the last line.
 * Nothing else moves the caret, and nothing it does touches the undo stack.
 */
class GotoLineBar : public QWidget
{
    Q_OBJECT

  public:
    explicit GotoLineBar(QWidget *parent = nullptr);

    /// Clears the field, says how many lines there are and takes focus.
    void prepare(int lineCount);
    /// Marks the field as holding something unusable.
    void setErrorState(bool isError);
    bool isErrorState() const;

    QString text() const;

    void reposition(const QWidget *viewport);

  signals:
    /// A usable `line` (1-based, already clamped) and `column` (1-based, 1
    /// when none was typed).
    void accepted(int line, int column);
    /// Enter on something unusable: 0, empty, or not a number.
    void rejected();
    void closeRequested();

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    bool handleBarKey(QKeyEvent *event);
    void accept();

    QLineEdit *m_input     {nullptr};
    int        m_lineCount {1};
};

/// Puts `bar` in the top-right of `viewport`, never wider than it minus a
/// margin on each side. Shared so the two overlays cannot drift apart.
void repositionOverlay(QWidget *bar, const QWidget *viewport);
/// The gap the overlays keep from the viewport edge. Named here because
/// both bars position against it and FindBar::fitToWidth() has to know
/// the same number the positioning uses.
inline constexpr int overlayMargin = 8;
} // namespace Kites
