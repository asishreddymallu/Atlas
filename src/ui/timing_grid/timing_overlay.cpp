#include "timing_overlay.h"

#include "config/app_settings.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Kites
{
namespace
{
constexpr int kGripHeight = 6;
}

TimingOverlay::TimingOverlay(const QString &title, QWidget *content, QWidget *parent) : QWidget(parent)
{
    // The stylesheet paints the surface (template.qss, "Timing overlay").
    setAttribute(Qt::WA_StyledBackground, true);

    m_grip = new QWidget(this);
    m_grip->setObjectName("timingOverlayGrip");
    m_grip->setAttribute(Qt::WA_StyledBackground, true);
    m_grip->setFixedHeight(kGripHeight);
    m_grip->setCursor(Qt::SizeVerCursor);
    m_grip->setToolTip("Drag to resize");
    m_grip->installEventFilter(this);

    auto *titleRow = new QWidget(this);
    titleRow->setObjectName("timingOverlayTitle");
    titleRow->setAttribute(Qt::WA_StyledBackground, true);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(10, 2, 6, 2);
    auto *label = new QLabel(title, titleRow);
    label->setObjectName("timingOverlayTitleLabel");
    titleLayout->addWidget(label);
    titleLayout->addStretch(1);
    m_close = new QToolButton(titleRow);
    m_close->setObjectName("timingOverlayClose");
    m_close->setText(QString::fromUtf8("✕"));
    m_close->setToolTip("Close the pipeline timing panel");
    m_close->setAccessibleName("Close pipeline timing");
    m_close->setAutoRaise(true);
    titleLayout->addWidget(m_close);
    connect(m_close, &QToolButton::clicked, this, &QWidget::hide);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_grip);
    layout->addWidget(titleRow);
    content->setParent(this);
    layout->addWidget(content, 1);

    m_height = std::max(0, AppSettings::getInstance().timingOverlayHeight());
}

void TimingOverlay::setHost(QWidget *host, QWidget *topLimit)
{
    if (m_host)
        m_host->removeEventFilter(this);
    m_host     = host;
    m_topLimit = topLimit;
    if (m_host)
        m_host->installEventFilter(this);
    // Not before it is shown: the window may not have its real size yet, and
    // the default height is taken from it.
    if (isVisible())
        reposition();
}

int TimingOverlay::maximumOverlayHeight() const
{
    if (!m_host)
        return kMinHeight;
    const int top = m_topLimit ? m_topLimit->geometry().bottom() + 1 : 0;
    return m_host->height() - top;
}

int TimingOverlay::clampHeight(int height) const
{
    // The minimum yields to the maximum: in a window too short for 150 px
    // the panel still never covers the ribbon or the menu.
    return std::min(std::max(height, kMinHeight), maximumOverlayHeight());
}

void TimingOverlay::reposition()
{
    if (!m_host)
        return;
    if (m_height <= 0)
        m_height = static_cast<int>(std::lround(window()->height() * kDefaultFraction));
    const int   height = clampHeight(m_height);
    // The host is a descendant of this widget's parent (the window).
    const QPoint origin = m_host->mapTo(parentWidget(), QPoint(0, 0));
    setGeometry(origin.x(), origin.y() + m_host->height() - height, m_host->width(), height);
    raise();
}

bool TimingOverlay::event(QEvent *event)
{
    switch (event->type())
    {
    case QEvent::Show:
        reposition();
        break;
    case QEvent::ShowToParent:
        emit visibilityChanged(true);
        break;
    case QEvent::HideToParent:
        emit visibilityChanged(false);
        break;
    default:
        break;
    }
    return QWidget::event(event);
}

bool TimingOverlay::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_host && (event->type() == QEvent::Resize || event->type() == QEvent::Move))
    {
        if (isVisible())
            reposition();
    }
    else if (watched == m_grip)
    {
        switch (event->type())
        {
        case QEvent::MouseButtonPress:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() != Qt::LeftButton)
                break;
            m_dragging        = true;
            m_dragStartY      = qRound(mouse->globalPosition().y());
            m_dragStartHeight = height();
            return true;
        }
        case QEvent::MouseMove:
        {
            if (!m_dragging)
                break;
            auto     *mouse = static_cast<QMouseEvent *>(event);
            const int dy    = qRound(mouse->globalPosition().y()) - m_dragStartY;
            m_height        = clampHeight(m_dragStartHeight - dy); // up is taller
            reposition();
            return true;
        }
        case QEvent::MouseButtonRelease:
        {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (!m_dragging || mouse->button() != Qt::LeftButton)
                break;
            m_dragging = false;
            AppSettings::getInstance().setTimingOverlayHeight(m_height);
            return true;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}
} // namespace Kites
