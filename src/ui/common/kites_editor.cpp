#include "kites_editor.h"

#include "gutter_column.h"
#include <QKeyEvent>
#include <QWheelEvent>
#include <algorithm>
#include "ui/theme/font_manager.h"
#include "ui/theme/theme_manager.h"
#include <QPainter>
#include <QTextBlock>
#include <QTextDocument>

namespace Kites
{
namespace
{
// spacing scale: 6px inset around the text body
constexpr int editorDocumentMargin = 6;
} // namespace

KitesEditor::KitesEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    connect(this, &KitesEditor::blockCountChanged, this, &KitesEditor::updateViewPortMargins);
    connect(this, &KitesEditor::updateRequest, this, &KitesEditor::updateGutterColumns);
    connect(this, &KitesEditor::cursorPositionChanged, this, &KitesEditor::highlightCurrentLine);
    // Through applyZoomedFont(), not straight to setFont(): a font changed
    // in Settings has to come back through the same path so the session
    // zoom delta is re-applied on top of it and the gutters are re-measured.
    connect(&FontManager::getInstance(), &FontManager::fontChangedSignal, this,
            [this](const QFont &) { applyZoomedFont(); });
    connect(&ThemeManager::getInstance(), &ThemeManager::editorThemeChangedSignal, this,
            &KitesEditor::themeChangedSlot);

    setFont(FontManager::getInstance().currentFont());
    applyEditorTheme();

    setTabStopDistance(editorTabWidth * fontMetrics().horizontalAdvance(' '));

    // Breathing room between the gutter and the first character, and above the
    // first line. Set on the document rather than as QSS padding: padding on a
    // QAbstractScrollArea shifts contentsRect(), which resizeEvent() uses to
    // place the gutters, so it would drag them along with the text.
    document()->setDocumentMargin(editorDocumentMargin);

    updateViewPortMargins();
    highlightCurrentLine();
}

void KitesEditor::updateViewPortMargins()
{
    setViewportMargins(leftViewMargin(), 0, rightViewMargin(), 0);
}
void KitesEditor::updateGutterColumns(const QRect &rect, int dy)
{
    if(dy)
    {
        for (auto *column : m_rightGutterColumns)
        {
            column->scroll(0, dy);
        }
        for (auto *column : m_leftGutterColumns)
        {
            column->scroll(0, dy);
        }
    }
    else
    {
        for (auto *column : m_rightGutterColumns)
        {
            column->update(0, rect.y(), column->width(), rect.height());
        }
        for (auto *column : m_leftGutterColumns)
        {
            column->update(0, rect.y(), column->width(), rect.height());
        }
    }
}

void KitesEditor::highlightCurrentLine()
{
    m_currentLineSelections.clear();

    // An empty document has no meaningful "current line", and the full-width
    // highlight would paint over the placeholder text that tells the user what
    // to do with the pane.
    if (!isReadOnly() && !document()->isEmpty())
    {
        QTextEdit::ExtraSelection selection;

        QColor lineColor = ThemeManager::getInstance().getCurrentLineColor();

        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        m_currentLineSelections.append(selection);
    }

    refreshExtraSelections();
}

void KitesEditor::setDiagnosticSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    m_diagnosticSelections = selections;
    refreshExtraSelections();
}

void KitesEditor::clearDiagnosticSelections()
{
    m_diagnosticSelections.clear();
    refreshExtraSelections();
}

void KitesEditor::setFindSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    m_findSelections = selections;
    refreshExtraSelections();
}

void KitesEditor::clearFindSelections()
{
    if (m_findSelections.isEmpty())
        return;
    m_findSelections.clear();
    refreshExtraSelections();
}

void KitesEditor::refreshExtraSelections()
{
    // current line first, then find highlights, then diagnostics: where
    // entries overlap Qt lets the later one win on conflicting format
    // properties, so the wave underline is never masked by the current-line
    // background OR by a match highlight, and a match highlight still covers
    // the current-line fill.
    QList<QTextEdit::ExtraSelection> merged = m_currentLineSelections;
    merged.append(m_findSelections);
    merged.append(m_diagnosticSelections);
    setExtraSelections(merged);
}

void KitesEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    int currentWidth = 0;
    for (auto *column : m_leftGutterColumns)
    {
        const int width = column->sizeHint().width();
        column->setGeometry(QRect(cr.left() + currentWidth, cr.top(), width, cr.height()));
        currentWidth += width;
    }
    currentWidth = 0;
    for(auto* column : m_rightGutterColumns)
    {
        const int width = column->sizeHint().width();
        column->setGeometry(QRect(cr.right() - currentWidth - width, cr.top(), width, cr.height()));
        currentWidth += width;
    }
}

void KitesEditor::themeChangedSlot([[maybe_unused]] const QString &themeId)
{
    applyEditorTheme();
    highlightCurrentLine();
}

int KitesEditor::zoomDelta() const
{
    return m_zoomDelta;
}

int KitesEditor::setZoomDelta(int delta)
{
    const int clamped = std::clamp(delta, minZoomDelta, maxZoomDelta);
    if (clamped != m_zoomDelta)
    {
        m_zoomDelta = clamped;
        applyZoomedFont();
    }
    return m_zoomDelta;
}

int KitesEditor::zoomBy(int steps)
{
    return setZoomDelta(m_zoomDelta + steps);
}

int KitesEditor::resetZoom()
{
    return setZoomDelta(0);
}

void KitesEditor::applyZoomedFont()
{
    // Always from the CONFIGURED font, so repeated zooms do not compound and
    // the setting itself is never written to.
    QFont zoomed = FontManager::getInstance().currentFont();
    const int base = zoomed.pixelSize() > 0 ? zoomed.pixelSize() : zoomed.pointSize();
    if (zoomed.pixelSize() > 0)
        zoomed.setPixelSize(std::max(1, base + m_zoomDelta));
    else
        zoomed.setPointSize(std::max(1, base + m_zoomDelta));
    setFont(zoomed);

    // The gutters get it explicitly. Qt propagates a font to children that
    // have none of their own, but the line-number column's sizeHint() is
    // read by updateViewPortMargins() in the next statement, and measured
    // it was still returning the old width - so the width is pushed rather
    // than waited for.
    for (GutterColumn *column : findChildren<GutterColumn *>())
        column->setFont(zoomed);

    // A tab is a number of SPACES wide, so it has to be re-measured, and the
    // gutters re-measured with it.
    setTabStopDistance(editorTabWidth * fontMetrics().horizontalAdvance(' '));
    updateViewPortMargins();
    for (GutterColumn *column : findChildren<GutterColumn *>())
        column->update();
    viewport()->update();
}

bool KitesEditor::handleZoomKey(QKeyEvent *event)
{
    if (!zoomEnabled())
        return false;

    const Qt::KeyboardModifiers mods = event->modifiers();
    // Ctrl, with or without Shift - a keyboard where + needs Shift must not
    // behave differently from one where it does not.
    if (mods != Qt::ControlModifier && mods != (Qt::ControlModifier | Qt::ShiftModifier))
        return false;

    switch (event->key())
    {
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        zoomBy(1);
        return true;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        zoomBy(-1);
        return true;
    case Qt::Key_0:
        resetZoom();
        return true;
    default:
        return false;
    }
}

void KitesEditor::wheelEvent(QWheelEvent *event)
{
    // Qt's own Ctrl+wheel zoom works on POINT size and this font is set in
    // pixels, so it does nothing here - measured on a bare editor. Handled
    // instead, which also means the gutters are re-measured with the text.
    if (zoomEnabled() && event->modifiers() == Qt::ControlModifier)
    {
        const int steps = event->angleDelta().y() > 0 ? 1 : -1;
        zoomBy(steps);
        event->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(event);
}

void KitesEditor::applyEditorTheme()
{
    // per-widget stylesheet so the editor can use a different theme than the
    // global UI (Zed-style editor/UI theme split); a per-widget palette is
    // ignored while an application stylesheet is active
    const ThemeManager &themeManager = ThemeManager::getInstance();
    setStyleSheet(QString("QPlainTextEdit { background-color: %1; color: %2; }")
                      .arg(themeManager.getEditorBackgroundColor().name(),
                           themeManager.getEditorForegroundColor().name()));

    // keep palette roles in sync for code that reads palette().color(QPalette::Base)
    QPalette editorPalette = palette();
    editorPalette.setColor(QPalette::Base, themeManager.getEditorBackgroundColor());
    editorPalette.setColor(QPalette::Text, themeManager.getEditorForegroundColor());
    setPalette(editorPalette);
    viewport()->update();
}

void KitesEditor::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        // Update the line number area when the palette changes
        highlightCurrentLine();
    }
    QPlainTextEdit::changeEvent(event);
}

void KitesEditor::addRightGutterColumn(GutterColumn* column)
{
    if (column && std::find(m_rightGutterColumns.begin(), 
    m_rightGutterColumns.end(), column) == m_rightGutterColumns.end())
    {
        m_rightGutterColumns.push_back(column);
    }
}

void KitesEditor::addLeftGutterColumn(GutterColumn* column)
{
    if (column && std::find(m_leftGutterColumns.begin(), 
    m_leftGutterColumns.end(), column) == m_leftGutterColumns.end())
    {
        m_leftGutterColumns.push_back(column);
    }
}

int KitesEditor::rightViewMargin() const
{
    int margin = 0;
    for (const auto *column : m_rightGutterColumns)
    {
        margin += column->sizeHint().width();
    }
    return margin;
}

int KitesEditor::leftViewMargin() const
{
    int margin = 0;
    for (const auto *column : m_leftGutterColumns)
    {
        margin += column->sizeHint().width();
    }
    return margin;
}
} // namespace Kites
