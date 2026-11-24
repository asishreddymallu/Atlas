#include "circuit_view.h"

#include <QGestureEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QNativeGestureEvent>
#include <QPinchGesture>
#include <QScrollBar>
#include <QToolButton>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace Atlas
{
CircuitView::CircuitView(QWidget *parent) : QGraphicsView(parent)
{
    init();
}

CircuitView::CircuitView(QGraphicsScene *scene, QWidget *parent) : QGraphicsView(scene, parent)
{
    init();
}

void CircuitView::init()
{
    setMouseTracking(true);
    // Vectors at every zoom: antialiased lines and text, no bitmap cache.
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setCacheMode(QGraphicsView::CacheNone);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setFocusPolicy(Qt::StrongFocus); // a click gives it focus, for the zoom keys
    viewport()->grabGesture(Qt::PinchGesture);

    m_buttons = new QWidget(this);
    m_buttons->setObjectName(QStringLiteral("circuitZoomButtons"));
    auto *row = new QHBoxLayout(m_buttons);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    auto make = [&](const QString &name, const QString &text, const QString &tip) {
        auto *b = new QToolButton(m_buttons);
        b->setObjectName(name);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(false);
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
        return b;
    };
    m_zoomOut   = make(QStringLiteral("circuitZoomOut"), QStringLiteral("−"), tr("Zoom out (Ctrl+minus)"));
    m_zoomIn    = make(QStringLiteral("circuitZoomIn"), QStringLiteral("+"), tr("Zoom in (Ctrl+plus)"));
    m_fitButton = make(QStringLiteral("circuitZoomFit"), tr("Fit"), tr("Fit the whole diagram (Ctrl+0)"));
    m_100       = make(QStringLiteral("circuitZoom100"), QStringLiteral("100%"), tr("Actual size"));
    connect(m_zoomOut, &QToolButton::clicked, this, &CircuitView::zoomOut);
    connect(m_zoomIn, &QToolButton::clicked, this, &CircuitView::zoomIn);
    connect(m_fitButton, &QToolButton::clicked, this, &CircuitView::zoomFit);
    connect(m_100, &QToolButton::clicked, this, &CircuitView::zoom100);
    m_buttons->adjustSize();
    applyMode();
}

double CircuitView::clampZoom(double z)
{
    return std::clamp(z, kMinZoom, kMaxZoom);
}

double CircuitView::zoom() const
{
    return transform().m11();
}

QRectF CircuitView::fitRect() const
{
    if (!scene())
        return {};
    return scene()->itemsBoundingRect().adjusted(-10, -10, 10, 10);
}

double CircuitView::fitZoom() const
{
    const QRectF r = fitRect();
    const QSize  v = viewport()->size();
    if (r.isEmpty() || v.isEmpty())
        return 1.0;
    return clampZoom(std::min(v.width() / r.width(), v.height() / r.height()));
}

void CircuitView::showScene(QGraphicsScene *s)
{
    setScene(s);
    m_fit = true;
    applyFit();
}

void CircuitView::applyMode()
{
    // Fit shows everything, so no scroll bars and nothing to drag.
    setHorizontalScrollBarPolicy(m_fit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(m_fit ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    setDragMode(m_fit ? QGraphicsView::NoDrag : QGraphicsView::ScrollHandDrag);
    if (m_fitButton)
        m_fitButton->setDown(m_fit);
}

void CircuitView::applyFit()
{
    if (!scene())
        return;
    const QRectF r = fitRect();
    // The scrollable area is the diagram itself (plus room to pan to an edge).
    setSceneRect(r);
    const double z = fitZoom();
    setTransform(QTransform::fromScale(z, z));
    centerOn(r.center());
    applyMode();
    emit zoomChanged(zoom(), m_fit);
}

void CircuitView::setZoom(double z)
{
    setZoomAt(z, viewport()->rect().center());
}

void CircuitView::setZoomAt(double z, const QPoint &anchor)
{
    if (!scene())
        return;
    z = clampZoom(z);
    const QPointF before = mapToScene(anchor);
    m_fit = false;
    setSceneRect(fitRect());
    applyMode();
    setTransform(QTransform::fromScale(z, z));
    // Put the scene point that was under the anchor back under it: centre
    // the view where that keeps `before` at `anchor`.
    const QPointF offset = QPointF(anchor - viewport()->rect().center()) / z;
    centerOn(before - offset);
    // Leaving Fit can bring the scroll bars in, which shrinks the viewport
    // after the first placement; correct for what moved (twice is enough).
    for (int i = 0; i < 2; ++i)
    {
        const QPointF drift = before - mapToScene(anchor);
        if (std::abs(drift.x()) * z < 0.5 && std::abs(drift.y()) * z < 0.5)
            break;
        centerOn(mapToScene(viewport()->rect().center()) + drift);
    }
    emit zoomChanged(zoom(), m_fit);
}

void CircuitView::zoomIn()
{
    setZoom(zoom() * kZoomStep);
}

void CircuitView::zoomOut()
{
    setZoom(zoom() / kZoomStep);
}

void CircuitView::zoomFit()
{
    m_fit = true;
    applyFit();
}

void CircuitView::zoom100()
{
    setZoom(1.0);
}

void CircuitView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers() & Qt::ControlModifier)
    {
        const double steps = event->angleDelta().y() / 120.0;
        if (steps != 0)
            setZoomAt(zoom() * std::pow(1.15, steps), event->position().toPoint());
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void CircuitView::keyPressEvent(QKeyEvent *event)
{
    // Only while the diagram has focus (this is its own key handler), so the
    // editor's Ctrl+plus/minus/0 are untouched.
    if (event->modifiers() & Qt::ControlModifier)
    {
        switch (event->key())
        {
        case Qt::Key_Plus:
        case Qt::Key_Equal: zoomIn(); event->accept(); return;
        case Qt::Key_Minus: zoomOut(); event->accept(); return;
        case Qt::Key_0: zoomFit(); event->accept(); return;
        default: break;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

void CircuitView::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    if (m_fit)
        applyFit();
    placeButtons();
}

void CircuitView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);
    if (m_fit)
        applyFit();
    placeButtons();
}

void CircuitView::placeButtons()
{
    if (!m_buttons)
        return;
    m_buttons->adjustSize();
    const QRect v = viewport()->geometry();
    m_buttons->move(v.right() - m_buttons->width() - 6, v.bottom() - m_buttons->height() - 6);
    m_buttons->raise();
}

bool CircuitView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::Gesture) // a touchscreen or touchpad pinch
    {
        auto *ge = static_cast<QGestureEvent *>(event);
        if (auto *pinch = static_cast<QPinchGesture *>(ge->gesture(Qt::PinchGesture)))
        {
            if (pinch->changeFlags() & QPinchGesture::ScaleFactorChanged)
                setZoomAt(zoom() * pinch->scaleFactor(),
                          viewport()->mapFromGlobal(pinch->centerPoint().toPoint()));
            return true;
        }
    }
    if (event->type() == QEvent::NativeGesture) // a macOS-style trackpad zoom
    {
        auto *g = static_cast<QNativeGestureEvent *>(event);
        if (g->gestureType() == Qt::ZoomNativeGesture)
        {
            setZoomAt(zoom() * (1.0 + g->value()), g->position().toPoint());
            return true;
        }
    }
    return QGraphicsView::viewportEvent(event);
}

} // namespace Atlas
