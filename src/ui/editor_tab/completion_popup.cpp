#include "ui/editor_tab/completion_popup.h"

#include "ui/theme/theme_manager.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace Kites
{
namespace
{
constexpr int framePad   = 1;  ///< the 1px border
constexpr int rowPadY    = 3;
constexpr int rowPadX    = 6;
constexpr int badgeGap   = 6;
constexpr int detailGap  = 12;
constexpr int minPopupWidth = 90;

/// The popup's palette, all from colours ThemeManager already exposes.
/// ThemeManager has no border/surface/accent getter, so these are derived
/// from the editor colours - see known_issues.
struct Palette
{
    QColor page;      ///< list background
    QColor border;
    QColor text;
    QColor matched;   ///< the typed characters
    QColor detail;
    QColor selected;  ///< selected row fill
};

QColor blend(const QColor &base, const QColor &tint, double amount)
{
    return QColor(int(base.red() + (tint.red() - base.red()) * amount),
                  int(base.green() + (tint.green() - base.green()) * amount),
                  int(base.blue() + (tint.blue() - base.blue()) * amount));
}

double luminance(const QColor &c)
{
    auto channel = [](double v)
    { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) +
           0.0722 * channel(c.blueF());
}

double contrastOf(const QColor &a, const QColor &b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

/**
 * @brief The dim detail colour, lifted until it is legible.
 *
 * mutedForeground is what a dim column wants to be, and measured it is
 * 2.91:1 on the list background and 1.63:1 on the selected row - VS Code's
 * detail text fails AA in the same way. Rather than ship that, the colour is
 * walked towards the ordinary text colour until it clears 4.5:1 on the
 * HARDER of the two grounds, so it stays dimmer than the label and stays
 * readable. Pure arithmetic from existing tokens; no new one is introduced.
 */
QColor legible(const QColor &want, const QColor &toward, const QColor &groundA,
               const QColor &groundB)
{
    QColor candidate = want;
    for (int step = 0; step <= 20; ++step)
    {
        candidate = blend(want, toward, step / 20.0);
        if (contrastOf(candidate, groundA) >= 4.5 && contrastOf(candidate, groundB) >= 4.5)
            return candidate;
    }
    return toward; // nothing closer to the wanted colour is legible here
}

Palette paletteNow()
{
    const ThemeManager &theme = ThemeManager::getInstance();
    Palette p;
    p.page     = theme.getEditorBackgroundColor();
    p.text     = theme.getEditorForegroundColor();
    p.matched  = theme.getInfoColor();
    // A visible frame without a new token: the page lifted towards the text.
    p.border   = blend(p.page, p.text, 0.35);
    // The same wash the find bar uses for a non-current match, which was
    // measured to keep ordinary text at 4.5:1 or better on every theme.
    p.selected = blend(p.page, theme.getInfoColor(), 0.25);
    p.detail   = legible(theme.getEditorMutedForegroundColor(), p.text, p.page, p.selected);
    // The accent is lifted the same way. Measured on the four real themes,
    // info on the selected row came to 4.18 on kites-dark and 4.19 on
    // kites-light - the in-tree test could not see it, because setTheme()
    // does nothing without theme resources and it measured one theme four
    // times. The harness is what found this.
    p.matched  = legible(theme.getInfoColor(), p.text, p.page, p.selected);
    return p;
}

QString badgeFor(Completion::Kind kind)
{
    switch (kind)
    {
    case Completion::Kind::Register:    return QStringLiteral("R");
    case Completion::Kind::Directive:   return QStringLiteral("D");
    case Completion::Kind::Label:       return QStringLiteral("L");
    case Completion::Kind::Instruction: break;
    }
    return QStringLiteral("I");
}
} // namespace

CompletionPopup::Colours CompletionPopup::colours()
{
    const Palette p = paletteNow();
    return Colours{p.page, p.border, p.text, p.matched, p.detail, p.selected};
}

CompletionPopup::CompletionPopup(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("completionPopup"));
    // No focus and no mouse grab: the editor keeps the caret, and every key
    // still reaches Editor::keyPressEvent.
    setFocusPolicy(Qt::NoFocus);
    setAttribute(Qt::WA_ShowWithoutActivating);
    hide();
}

void CompletionPopup::setEditorFont(const QFont &font)
{
    setFont(font);
    // setFont() alone is not enough: the application stylesheet carries
    // QWidget { font-size: ... }, and a stylesheet rule beats setFont(). The
    // list rendered at the chrome's size at every zoom level until the size
    // went into its OWN sheet - the same trap the editor hit, and kites_test
    // cannot see it because it has no application stylesheet. The harness
    // screenshots are what showed it.
    const bool    inPixels = font.pixelSize() > 0;
    const int     size     = inPixels ? font.pixelSize() : font.pointSize();
    const QString unit     = inPixels ? QStringLiteral("px") : QStringLiteral("pt");
    setStyleSheet(QStringLiteral("font-size: %1%2;").arg(size).arg(unit));
    updateGeometry();
    update();
}

int CompletionPopup::rowHeight() const
{
    return fontMetrics().height() + 2 * rowPadY;
}

void CompletionPopup::setCandidates(const QList<Completion::Item> &items,
                                    const QList<Completion::Match> &matches)
{
    const QString keep = selectedLabel();

    m_rows.clear();
    m_rows.reserve(matches.size());
    for (const Completion::Match &m : matches)
    {
        if (m.index < 0 || m.index >= items.size())
            continue;
        m_rows.push_back(Row{items.at(m.index), m.matched});
    }

    // VS Code keeps the selection on the same item while the filter narrows,
    // and falls back to the first when it is gone.
    m_current = 0;
    if (!keep.isEmpty())
    {
        for (int i = 0; i < int(m_rows.size()); ++i)
        {
            if (m_rows.at(i).item.label == keep)
            {
                m_current = i;
                break;
            }
        }
    }
    m_top = 0;
    ensureVisible();
    update();
}

void CompletionPopup::setCurrentRow(int row)
{
    if (m_rows.isEmpty())
        return;
    m_current = std::clamp(row, 0, int(m_rows.size()) - 1);
    ensureVisible();
    update();
}

void CompletionPopup::moveSelection(int delta)
{
    if (m_rows.isEmpty())
        return;
    const int n = int(m_rows.size());
    // Wrapping is the point: Down on the last row goes to the first.
    m_current = ((m_current + delta) % n + n) % n;
    ensureVisible();
    update();
}

void CompletionPopup::movePage(int direction)
{
    if (m_rows.isEmpty())
        return;
    const int page = std::max(1, std::min(int(m_rows.size()), maxVisibleRows));
    const int n    = int(m_rows.size());
    m_current      = std::clamp(m_current + direction * page, 0, n - 1);
    ensureVisible();
    update();
}

QString CompletionPopup::selectedLabel() const
{
    if (m_current < 0 || m_current >= int(m_rows.size()))
        return {};
    return m_rows.at(m_current).item.label;
}

void CompletionPopup::ensureVisible()
{
    const int visible = std::min(int(m_rows.size()), maxVisibleRows);
    if (visible <= 0)
    {
        m_top = 0;
        return;
    }
    if (m_current < m_top)
        m_top = m_current;
    else if (m_current >= m_top + visible)
        m_top = m_current - visible + 1;
    m_top = std::clamp(m_top, 0, std::max(0, int(m_rows.size()) - visible));
}

int CompletionPopup::contentWidth() const
{
    const QFontMetrics fm(font());
    int widest = 0;
    for (const Row &row : m_rows)
    {
        int w = fm.horizontalAdvance(QStringLiteral("M")) + badgeGap; // badge
        w += fm.horizontalAdvance(row.item.label);
        if (!row.item.detail.isEmpty())
            w += detailGap + fm.horizontalAdvance(row.item.detail);
        widest = std::max(widest, w);
    }
    return widest + 2 * (rowPadX + framePad);
}

void CompletionPopup::placeFor(const QRect &caretLine, int wordLeft, const QSize &viewportSize)
{
    const int visible = std::min(int(m_rows.size()), maxVisibleRows);
    if (visible <= 0)
    {
        hide();
        return;
    }

    const int wanted = contentWidth();
    const int width  = std::clamp(wanted, std::min(minPopupWidth, viewportSize.width()),
                                  viewportSize.width());
    m_detailDropped  = wanted > width;

    int height = visible * rowHeight() + 2 * framePad;

    // Below the caret line by default; above it when there is no room, and
    // shrunk rather than allowed to hang outside either way.
    const int below = viewportSize.height() - caretLine.bottom();
    const int above = caretLine.top();
    m_above = false;
    if (height > below && above > below)
    {
        m_above = true;
        height  = std::min(height, above);
    }
    else
    {
        height = std::min(height, std::max(0, below));
    }
    // Never fewer than one row, and never taller than the viewport.
    height = std::clamp(height, rowHeight() + 2 * framePad, viewportSize.height());

    const int x = std::clamp(wordLeft, 0, std::max(0, viewportSize.width() - width));
    const int y = m_above ? std::max(0, caretLine.top() - height) : caretLine.bottom();

    setGeometry(x, std::min(y, std::max(0, viewportSize.height() - height)), width, height);
    show();
    raise();
    update();
}

void CompletionPopup::paintEvent(QPaintEvent *)
{
    // A plain QWidget paints neither a background nor a border from a
    // stylesheet, and this one has no stylesheet anyway - it is drawn here so
    // it is opaque over the code behind it.
    QPainter painter(this);
    const Palette palette = paletteNow();

    painter.fillRect(rect(), palette.page);
    painter.setPen(palette.border);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QFontMetrics fm(font());
    const int rh      = rowHeight();
    const int visible = std::min(int(m_rows.size()), std::max(0, (height() - 2 * framePad) / rh));

    for (int i = 0; i < visible; ++i)
    {
        const int index = m_top + i;
        if (index >= int(m_rows.size()))
            break;
        const Row &row = m_rows.at(index);
        const QRect band(framePad, framePad + i * rh, width() - 2 * framePad, rh);

        if (index == m_current)
            painter.fillRect(band, palette.selected);

        int x = band.left() + rowPadX;
        const int baseline = band.top() + rowPadY + fm.ascent();

        // ── kind badge, a single dim letter (no icon set fits these)
        painter.setPen(palette.detail);
        painter.drawText(x, baseline, badgeFor(row.item.kind));
        x += fm.horizontalAdvance(QStringLiteral("M")) + badgeGap;

        // ── the detail is dropped FIRST when the row is too narrow
        int detailWidth = 0;
        if (!row.item.detail.isEmpty() && !m_detailDropped)
            detailWidth = detailGap + fm.horizontalAdvance(row.item.detail);

        const int labelRoom = band.right() - rowPadX - detailWidth - x;
        // ── the label is elided SECOND
        const QString shown = fm.elidedText(row.item.label, Qt::ElideRight, std::max(0, labelRoom));

        // Matched characters in the accent colour and bold; the rest plain.
        QFont plain = font();
        QFont bold  = font();
        bold.setBold(true);
        int        drawX = x;
        const bool elided = shown != row.item.label;
        for (int c = 0; c < shown.size(); ++c)
        {
            const bool hit = !elided && row.matched.contains(c);
            painter.setFont(hit ? bold : plain);
            painter.setPen(hit ? palette.matched : palette.text);
            const QString ch = shown.mid(c, 1);
            painter.drawText(drawX, baseline, ch);
            drawX += QFontMetrics(hit ? bold : plain).horizontalAdvance(ch);
        }
        painter.setFont(plain);

        if (detailWidth > 0)
        {
            painter.setPen(palette.detail);
            painter.drawText(band.right() - rowPadX - fm.horizontalAdvance(row.item.detail),
                             baseline, row.item.detail);
        }
    }

    // A thin scrollbar when there is more than fits, in the same dim colour.
    if (int(m_rows.size()) > visible && visible > 0)
    {
        const int trackH = height() - 2 * framePad;
        const int thumbH = std::max(8, trackH * visible / int(m_rows.size()));
        const int thumbY = framePad + trackH * m_top / int(m_rows.size());
        QColor thumb = palette.detail;
        painter.fillRect(QRect(width() - framePad - 3, thumbY, 3, thumbH), thumb);
    }
}

void CompletionPopup::mousePressEvent(QMouseEvent *event)
{
    const int rh = rowHeight();
    if (rh <= 0)
        return;
    const int row = m_top + (event->position().toPoint().y() - framePad) / rh;
    if (row >= 0 && row < int(m_rows.size()))
    {
        m_current = row;
        emit rowActivated(m_rows.at(row).item.label);
    }
    event->accept();
}

void CompletionPopup::wheelEvent(QWheelEvent *event)
{
    const int steps = event->angleDelta().y() > 0 ? -1 : 1;
    const int visible = std::min(int(m_rows.size()), maxVisibleRows);
    m_top = std::clamp(m_top + steps, 0, std::max(0, int(m_rows.size()) - visible));
    update();
    event->accept();
}

} // namespace Kites
