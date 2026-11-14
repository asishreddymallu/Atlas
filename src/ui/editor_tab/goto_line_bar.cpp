#include "goto_line_bar.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QStyle>
#include <algorithm>

namespace Kites
{
namespace
{
} // namespace

void repositionOverlay(QWidget *bar, const QWidget *viewport)
{
    if (!bar || !viewport)
        return;
    bar->adjustSize();
    const int available = viewport->width() - 2 * overlayMargin;
    const int width     = std::min(bar->sizeHint().width(), std::max(0, available));
    bar->resize(width, bar->sizeHint().height());
    bar->move(std::max(overlayMargin, viewport->width() - width - overlayMargin),
              overlayMargin);
}

GotoLineBar::GotoLineBar(QWidget *parent) : QWidget(parent)
{
    setObjectName("gotoLineBar");
    setAutoFillBackground(true);

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(4);

    m_input = new QLineEdit(this);
    m_input->setObjectName("gotoLineInput");
    m_input->installEventFilter(this);
    row->addWidget(m_input, 1);

    connect(m_input, &QLineEdit::textEdited, this, [this] { setErrorState(false); });
    hide();
}

void GotoLineBar::prepare(int lineCount)
{
    m_lineCount = std::max(1, lineCount);
    m_input->clear();
    m_input->setPlaceholderText(tr("Go to line (1-%1)").arg(m_lineCount));
    setErrorState(false);
    m_input->setFocus();
}

void GotoLineBar::setErrorState(bool isError)
{
    // A property, so the stylesheet picks the colour per theme.
    m_input->setProperty("findError", isError);
    m_input->style()->unpolish(m_input);
    m_input->style()->polish(m_input);
}

bool GotoLineBar::isErrorState() const
{
    return m_input->property("findError").toBool();
}

QString GotoLineBar::text() const
{
    return m_input->text();
}

void GotoLineBar::reposition(const QWidget *viewport)
{
    repositionOverlay(this, viewport);
}

void GotoLineBar::accept()
{
    const QString typed = m_input->text().trimmed();
    if (typed.isEmpty())
    {
        setErrorState(true);
        emit rejected();
        return;
    }

    const QStringList parts = typed.split(QLatin1Char(':'));
    bool              ok    = false;
    const int         line  = parts.at(0).trimmed().toInt(&ok);
    if (!ok || line < 1)
    {
        setErrorState(true);
        emit rejected();
        return;
    }

    int column = 1;
    if (parts.size() > 1)
    {
        bool      columnOk = false;
        const int typedColumn = parts.at(1).trimmed().toInt(&columnOk);
        // A bad column is not a bad jump: the line is still usable.
        if (columnOk && typedColumn >= 1)
            column = typedColumn;
    }

    setErrorState(false);
    // Past the end goes to the last line rather than refusing.
    emit accepted(std::min(line, m_lineCount), column);
}

bool GotoLineBar::handleBarKey(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && event->modifiers() == Qt::NoModifier)
    {
        emit closeRequested();
        return true;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        event->modifiers() == Qt::NoModifier)
    {
        accept();
        return true;
    }
    return false;
}

bool GotoLineBar::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && handleBarKey(static_cast<QKeyEvent *>(event)))
        return true;
    return QWidget::eventFilter(watched, event);
}

void GotoLineBar::keyPressEvent(QKeyEvent *event)
{
    if (handleBarKey(event))
    {
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}
} // namespace Kites
