#pragma once
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
protected:
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
    // the single place that calls QPlainTextEdit::setExtraSelections()
    void refreshExtraSelections();
    std::vector<GutterColumn*> m_rightGutterColumns{};
    std::vector<GutterColumn*> m_leftGutterColumns{};
    QList<QTextEdit::ExtraSelection> m_currentLineSelections{};
    QList<QTextEdit::ExtraSelection> m_diagnosticSelections{};
};
} // namespace Kites