#include "ui/editor_tab/label_overlays.h"

#include "ui/editor_tab/goto_line_bar.h" // repositionOverlay, overlayMargin
#include "ui/theme/theme_manager.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>

namespace Kites
{
namespace
{
constexpr int framePad = 1;
constexpr int rowPadY  = 3;
constexpr int rowPadX  = 6;

/// The header's full text. One definition, so the width that is reserved
/// for it and the text that is drawn can never be different strings - they
/// were, and at max zoom the header read "loop - 4" with `found` clipped.
QString headerText(const QString &name, int count)
{
    return QCoreApplication::translate("Kites::ReferencesBar", "%1 - %2 found")
        .arg(name)
        .arg(count);
}

/// Width of the line-number gutter inside the list, at `fm`.
int numberColumn(const QFontMetrics &fm, const QList<int> &lines)
{
    int widest = 0;
    for (int line : lines)
        widest = std::max(widest, fm.horizontalAdvance(QString::number(line + 1)));
    return widest + 8;
}

QColor blend(const QColor &base, const QColor &tint, double amount)
{
    return QColor(int(base.red() + (tint.red() - base.red()) * amount),
                  int(base.green() + (tint.green() - base.green()) * amount),
                  int(base.blue() + (tint.blue() - base.blue()) * amount));
}

/// The overlay palette, from colours ThemeManager already exposes. No new
/// token; the same derivation the suggestion list uses.
struct Palette
{
    QColor page, border, text, dim, selected;
};
Palette paletteNow()
{
    const ThemeManager &theme = ThemeManager::getInstance();
    Palette p;
    p.page     = theme.getEditorBackgroundColor();
    p.text     = theme.getEditorForegroundColor();
    p.dim      = theme.getEditorMutedForegroundColor();
    p.border   = blend(p.page, p.text, 0.35);
    p.selected = blend(p.page, theme.getInfoColor(), 0.25);
    return p;
}
} // namespace

// ═════════════════════════ ReferencesBar ═════════════════════════════════

ReferencesBar::ReferencesBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("referencesBar"));
    setFocusPolicy(Qt::NoFocus); // the editor keeps the caret
    hide();
}

ReferencesBar::Colours ReferencesBar::colours()
{
    const Palette p = paletteNow();
    return {p.page, p.border, p.text, p.dim, p.selected};
}

void ReferencesBar::setRows(const QList<Row> &rows, const QString &name)
{
    m_rows    = rows;
    m_name    = name;
    m_current = 0;
    m_top     = 0;
    update();
}

const ReferencesBar::Row *ReferencesBar::selected() const
{
    if (m_current < 0 || m_current >= int(m_rows.size()))
        return nullptr;
    return &m_rows.at(m_current);
}

void ReferencesBar::setCurrentRow(int row)
{
    if (row < 0 || row >= int(m_rows.size()))
        return;
    m_current = row;
    ensureVisible();
    update();
}

void ReferencesBar::moveSelection(int delta)
{
    if (m_rows.isEmpty())
        return;
    const int n = int(m_rows.size());
    m_current   = ((m_current + delta) % n + n) % n;
    ensureVisible();
    update();
}

void ReferencesBar::ensureVisible()
{
    const int visible = std::min(int(m_rows.size()), maxVisibleRows);
    if (visible <= 0)
        return;
    if (m_current < m_top)
        m_top = m_current;
    else if (m_current >= m_top + visible)
        m_top = m_current - visible + 1;
    m_top = std::clamp(m_top, 0, std::max(0, int(m_rows.size()) - visible));
}

void ReferencesBar::setEditorFont(const QFont &font)
{
    setFont(font);
    // setFont() alone loses to the application stylesheet's
    // QWidget { font-size }, the same trap the suggestion list hit.
    const bool    inPixels = font.pixelSize() > 0;
    const int     size     = inPixels ? font.pixelSize() : font.pointSize();
    const QString unit     = inPixels ? QStringLiteral("px") : QStringLiteral("pt");
    setStyleSheet(QStringLiteral("font-size: %1%2;").arg(size).arg(unit));
    update();
}

int ReferencesBar::rowHeight() const
{
    return fontMetrics().height() + 2 * rowPadY;
}

void ReferencesBar::reposition(const QWidget *viewport, int topOffset)
{
    if (!viewport || m_rows.isEmpty())
        return;
    const QFontMetrics fm(font());
    QList<int>         lines;
    for (const Row &row : m_rows)
        lines << row.line;
    // The header is measured, not guessed at with a constant: at max zoom a
    // guess of +80 clipped `found` off the end of it.
    int widest = fm.horizontalAdvance(headerText(m_name, int(m_rows.size())));
    const int numbers = numberColumn(fm, lines);
    const int tag     = fm.horizontalAdvance(tr("def")) + rowPadX;
    for (const Row &row : m_rows)
    {
        // Room for the `def` tag as well, or it is painted over the text -
        // which it was, at max zoom.
        widest = std::max(widest, numbers + fm.horizontalAdvance(row.text) +
                                      (row.isDefinition ? tag : 0));
    }
    const int available = viewport->width() - 2 * overlayMargin;
    const int width     = std::clamp(widest + 2 * (rowPadX + framePad), 120, available);
    const int visible   = std::min(int(m_rows.size()), maxVisibleRows);
    const int top       = overlayMargin + std::max(0, topOffset);
    // One row for the header, then the list.
    const int height =
        std::min((visible + 1) * rowHeight() + 2 * framePad,
                 std::max(rowHeight(), viewport->height() - top - overlayMargin));
    setGeometry(std::max(overlayMargin, viewport->width() - width - overlayMargin),
                top, width, height);
    show();
    raise();
}

bool ReferencesBar::handleKey(QKeyEvent *event)
{
    if (!isVisible())
        return false;
    switch (event->key())
    {
    case Qt::Key_Up:
        moveSelection(-1);
        return true;
    case Qt::Key_Down:
        moveSelection(1);
        return true;
    case Qt::Key_Escape:
        emit closeRequested();
        return true;
    case Qt::Key_Enter:
    case Qt::Key_Return:
        if (const Row *row = selected())
            emit picked(row->offset);
        return true;
    default:
        break;
    }
    return false;
}

void ReferencesBar::paintEvent(QPaintEvent *)
{
    QPainter      painter(this);
    const Palette palette = paletteNow();
    painter.fillRect(rect(), palette.page);
    painter.setPen(palette.border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QFontMetrics fm(font());
    const int          rh = rowHeight();
    QList<int>         lines;
    for (const Row &row : m_rows)
        lines << row.line;

    // Header: what is being listed and how many. Elided rather than
    // clipped, for the case where the viewport is too narrow to hold it.
    painter.setPen(palette.dim);
    painter.drawText(framePad + rowPadX, framePad + rowPadY + fm.ascent(),
                     fm.elidedText(headerText(m_name, int(m_rows.size())),
                                   Qt::ElideRight,
                                   std::max(0, width() - 2 * (framePad + rowPadX))));

    const int visible =
        std::min(int(m_rows.size()), std::max(0, (height() - 2 * framePad) / rh - 1));
    for (int i = 0; i < visible; ++i)
    {
        const int index = m_top + i;
        if (index >= int(m_rows.size()))
            break;
        const Row  &row = m_rows.at(index);
        const QRect band(framePad, framePad + (i + 1) * rh, width() - 2 * framePad, rh);
        if (index == m_current)
            painter.fillRect(band, palette.selected);

        // The dim colour is for the UNSELECTED rows only. Measured against
        // the selected band it is 2.69-3.46 across the four themes, under
        // AA; the selected row uses the full text colour, which is 5.33 at
        // worst. So dim is never painted on that ground.
        const QColor secondary = index == m_current ? palette.text : palette.dim;

        const QString number = QString::number(row.line + 1);
        painter.setPen(secondary);
        painter.drawText(band.left() + rowPadX, band.top() + rowPadY + fm.ascent(), number);

        // The same column width reposition() reserved, from the same
        // function - a hard-coded "0000" was wider than the numbers at a
        // small font and narrower than them at a large one.
        const int textLeft = band.left() + rowPadX + numberColumn(fm, lines);
        const QString tag      = tr("def");
        const int     tagWidth = row.isDefinition
                                     ? fm.horizontalAdvance(tag) + rowPadX
                                     : 0;
        painter.setPen(palette.text);
        const QString shown =
            fm.elidedText(row.text, Qt::ElideRight,
                          std::max(0, band.right() - rowPadX - tagWidth - textLeft));
        painter.drawText(textLeft, band.top() + rowPadY + fm.ascent(), shown);

        if (row.isDefinition)
        {
            painter.setPen(secondary);
            painter.drawText(band.right() - rowPadX - fm.horizontalAdvance(tag),
                             band.top() + rowPadY + fm.ascent(), tag);
        }
    }
}

void ReferencesBar::mousePressEvent(QMouseEvent *event)
{
    const int rh = rowHeight();
    if (rh <= 0)
        return;
    const int row = m_top + (event->position().toPoint().y() - framePad) / rh - 1;
    if (row >= 0 && row < int(m_rows.size()))
    {
        m_current = row;
        emit picked(m_rows.at(row).offset);
    }
    event->accept();
}

void ReferencesBar::wheelEvent(QWheelEvent *event)
{
    const int steps   = event->angleDelta().y() > 0 ? -1 : 1;
    const int visible = std::min(int(m_rows.size()), maxVisibleRows);
    m_top = std::clamp(m_top + steps, 0, std::max(0, int(m_rows.size()) - visible));
    update();
    event->accept();
}

// ═══════════════════════════ RenameBar ═══════════════════════════════════

RenameBar::RenameBar(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("renameBar"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(6, 4, 6, 4);
    column->setSpacing(2);

    m_input = new QLineEdit(this);
    m_input->setObjectName(QStringLiteral("renameInput"));
    m_input->installEventFilter(this);
    column->addWidget(m_input);

    m_reason = new QLabel(this);
    m_reason->setObjectName(QStringLiteral("renameReason"));
    m_reason->hide();
    column->addWidget(m_reason);

    hide();
}

void RenameBar::openOn(const QString &name)
{
    clearReason();
    m_input->setText(name);
    m_input->selectAll();
    show();
    raise();
    m_input->setFocus();
}

QString RenameBar::newName() const
{
    return m_input->text();
}

void RenameBar::showReason(const QString &reason)
{
    m_reason->setText(reason);
    m_reason->show();
    adjustSize();
}

void RenameBar::clearReason()
{
    m_reason->clear();
    m_reason->hide();
}

void RenameBar::setEditorFont(const QFont &font)
{
    setFont(font);
    const bool    inPixels = font.pixelSize() > 0;
    const int     size     = inPixels ? font.pixelSize() : font.pointSize();
    const QString unit     = inPixels ? QStringLiteral("px") : QStringLiteral("pt");
    setStyleSheet(QStringLiteral("font-size: %1%2;").arg(size).arg(unit));
}

void RenameBar::reposition(const QWidget *viewport)
{
    repositionOverlay(this, viewport);
}

bool RenameBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_input && event->type() == QEvent::KeyPress)
    {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape)
        {
            emit closeRequested();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            emit accepted();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RenameBar::paintEvent(QPaintEvent *event)
{
    // Same reason as FindBar: a plain QWidget paints no stylesheet
    // background, so the overlay would be see-through over the code.
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    const Palette palette = paletteNow();
    painter.fillRect(rect(), palette.page);
    painter.setPen(palette.border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    QWidget::paintEvent(event);
}

// ═════════════════════════ the rename rules ══════════════════════════════

RenameCheck checkRename(const QString &oldName, const QString &newName,
                        const LabelIndex::Index &index)
{
    if (newName.isEmpty())
        return {false, QObject::tr("the new name is empty")};

    for (QChar c : newName)
    {
        if (!LabelIndex::isLabelChar(c))
            return {false, QObject::tr("'%1' cannot be in a label name").arg(c)};
    }
    // The assembler's lexer sends a leading digit to number(), so a name
    // starting with one can never be read back as a label.
    if (newName.at(0).isDigit())
        return {false, QObject::tr("a label cannot start with a digit")};

    if (newName == oldName)
        return {true, {}}; // a no-op, handled by the caller

    const QList<LabelIndex::Definition> clash = index.definitionsOf(newName);
    if (!clash.isEmpty())
        return {false, QObject::tr("'%1' is already defined on line %2")
                           .arg(newName)
                           .arg(clash.first().line + 1)};

    // A name that lexes as a register can be DEFINED but never referenced -
    // the lexer resolves a register before it considers a label reference -
    // so renaming to one would silently break every use.
    static const QStringList registerLike = []
    {
        QStringList names;
        for (int i = 0; i < 32; ++i)
            names << QStringLiteral("x%1").arg(i) << QStringLiteral("f%1").arg(i);
        names << "zero" << "ra" << "sp" << "gp" << "tp" << "fp";
        for (int i = 0; i < 8; ++i)
            names << QStringLiteral("t%1").arg(i) << QStringLiteral("a%1").arg(i)
                  << QStringLiteral("s%1").arg(i);
        return names;
    }();
    if (registerLike.contains(newName, Qt::CaseInsensitive))
        return {false, QObject::tr("'%1' is a register name").arg(newName)};

    return {true, {}};
}

} // namespace Kites
