#pragma once
class QKeyEvent;
class QWheelEvent;

#include <QPlainTextEdit>
#include <QTextEdit>
namespace Kites
{
/// How many columns a tab stands for in every Kites editor. It sets the tab
/// stop distance here, and it is what one outdent removes from a line that
/// is indented with spaces instead - the same number, named once rather than
/// typed in two places.
inline constexpr int editorTabWidth = 4;

class GutterColumn; //forward declaration
class KitesEditor : public QPlainTextEdit
{
    Q_OBJECT
    friend class GutterColumn;
public:
    explicit KitesEditor(QWidget *parent = nullptr);
    ~KitesEditor() {};

    // Diagnostic squiggles are kept in their own list so that moving the cursor
    // (which recomputes only the current-line highlight) can no longer erase
    // them. Both lists are merged by refreshExtraSelections().
    void setDiagnosticSelections(const QList<QTextEdit::ExtraSelection> &selections);
    void clearDiagnosticSelections();

    /**
     * @brief The find bar's highlights, kept in their own list.
     *
     * Third list, same reason as the second: refreshExtraSelections() is the
     * only place that calls QPlainTextEdit::setExtraSelections(), so a
     * caller that set the selections itself would silently wipe the other
     * two. Find highlights go UNDER the diagnostics in the merged list, so a
     * squiggle is never masked by a match highlight, and over the
     * current-line fill.
     */
    void setFindSelections(const QList<QTextEdit::ExtraSelection> &selections);
    void clearFindSelections();

    /// The bracket highlight, between the find matches and the diagnostics:
    /// a match highlight should not hide the bracket, and a squiggle should
    /// not be hidden by it.
    void setBracketSelections(const QList<QTextEdit::ExtraSelection> &selections);

    /**
     * @brief Session zoom, as a delta on the configured font size.
     *
     * Here, in src/ui/common, because the GUTTERS are here: a font change
     * has to re-measure the line-number column and re-centre the breakpoint
     * dot, and updateViewPortMargins() is what does that.
     *
     * A delta, never a new size, so the font chosen in Settings is untouched
     * and a zoom is forgotten when the window closes.
     *
     * Clamped to [-6, +18] AROUND whatever the configured size is, rather
     * than to absolute sizes, because the configured size is the user's.
     * Six steps down is where the line numbers stop being legible and the
     * 9 px breakpoint dot stops fitting its line; eighteen up is where an
     * 849 px window shows almost nothing. Measured at both ends.
     */
    static constexpr int minZoomDelta = -6;
    static constexpr int maxZoomDelta = 18;

    int  zoomDelta() const;
    /// Clamps, applies and re-measures. Returns what it landed on, which is
    /// not `delta` at the ends.
    int  setZoomDelta(int delta);
    int  zoomBy(int steps);
    int  resetZoom();

protected:
    /**
     * @brief Whether this pane answers the zoom keys and Ctrl+wheel.
     *
     * False by default. The Input, Expanded and Output panes say yes; the
     * profiler and compiler views are left exactly as they were, which is
     * why this is opt-in rather than a blanket change to every editor.
     */
    virtual bool zoomEnabled() const { return false; }
    /// True when the event was a zoom key and has been dealt with.
    bool handleZoomKey(QKeyEvent *event);
    void wheelEvent(QWheelEvent *event) override;

    void updateViewPortMargins();
    void updateGutterColumns(const QRect &rect, int dy);
    void highlightCurrentLine();
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

    void addRightGutterColumn(GutterColumn* column);
    void addLeftGutterColumn(GutterColumn* column);

    int rightViewMargin() const;
    int leftViewMargin() const;
private slots:
    void themeChangedSlot(const QString &themeId);
private:
    void applyEditorTheme();
    /// Re-reads the configured font, applies the delta, re-measures.
    void applyZoomedFont();

    int m_zoomDelta {0};
    // the single place that calls QPlainTextEdit::setExtraSelections()
    void refreshExtraSelections();
    std::vector<GutterColumn*> m_rightGutterColumns{};
    std::vector<GutterColumn*> m_leftGutterColumns{};
    QList<QTextEdit::ExtraSelection> m_currentLineSelections{};
    QList<QTextEdit::ExtraSelection> m_findSelections{};
    QList<QTextEdit::ExtraSelection> m_bracketSelections{};
    QList<QTextEdit::ExtraSelection> m_diagnosticSelections{};
};
} // namespace Kites