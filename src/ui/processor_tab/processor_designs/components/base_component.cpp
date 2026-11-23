#include "base_component.h"
#include "ui/theme/theme_manager.h"
#include <QPen>
#include <cmath>
#include <QJsonObject>
#include <QPainter>
#include <QTextDocument>
#include <QTextOption>
#include <algorithm>

namespace Atlas
{
BaseComponent::BaseComponent(const QPainterPath &path, const QString &name, QGraphicsItem *parent)
    : QGraphicsPathItem(path, parent)
{

    // setFlag(QGraphicsItem::ItemIsSelectable, false);
    // setFlag(QGraphicsItem::ItemIsMovable, true);

    m_name = name;
    if (name != "")
    {
        m_label = new QGraphicsTextItem(name, this);
        QFont labelFont = m_label->font();
        labelFont.setStretch(130);
        m_label->setFont(labelFont);
        QRectF box = path.boundingRect();
        const qreal labelWidth = std::max<qreal>(box.width() * 1.6, 80.0);
        m_label->setTextWidth(labelWidth);
        QTextOption option = m_label->document()->defaultTextOption();
        option.setAlignment(Qt::AlignHCenter);
        m_label->document()->setDefaultTextOption(option);
        m_label->setPos(box.center().x() - labelWidth / 2,
                        box.center().y() - m_label->boundingRect().height() / 2);
    }
    applyThemeColors();
}

void BaseComponent::applyThemeColors()
{
    // The original diagram: gray, white outline (DiagramColors' defaults).
    const DiagramColors d = ThemeManager::getInstance().diagramColors();
    setBrush(d.component);
    setPen(QPen(d.componentBorder, 2));
    if (m_label)
        m_label->setDefaultTextColor(nameColorFor(d));
}

QColor BaseComponent::nameColorFor(const DiagramColors &colors)
{
    if (colors.label.isValid())
        return colors.label;
    auto lum = [](const QColor &c) {
        auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    // Black reads better than white on a fill brighter than mid-grey.
    const double l = lum(colors.component);
    return (l + 0.05) / 0.05 >= 1.05 / (l + 0.05) ? QColor(Qt::black) : QColor(Qt::white);
}

void BaseComponent::setName(const QString &name)
{
    m_name = name;
    if (name != "")
    {
        m_label = new QGraphicsTextItem(name, this);
        QFont labelFont = m_label->font();
        labelFont.setStretch(130);
        m_label->setFont(labelFont);
        QRectF box = path().boundingRect();
        const qreal labelWidth = std::max<qreal>(box.width() * 1.6, 80.0);
        m_label->setTextWidth(labelWidth);
        QTextOption option = m_label->document()->defaultTextOption();
        option.setAlignment(Qt::AlignHCenter);
        m_label->document()->setDefaultTextOption(option);
        m_label->setPos(box.center().x() - labelWidth / 2,
                        box.center().y() - m_label->boundingRect().height() / 2);
        applyThemeColors();
    }
}

QRectF BaseComponent::insideNameRect() const
{
    if (!m_label)
        return {};
    // The text item is wider than its text (centred in textWidth): the ink.
    const QRectF box   = m_label->boundingRect();
    const qreal  ink   = std::min(box.width(), m_label->document()->idealWidth());
    const QRectF local = QRectF(box.left() + (box.width() - ink) / 2, box.top(), ink, box.height());
    return m_label->mapRectToScene(local);
}

QJsonObject BaseComponent::toJson()
{
    QJsonObject obj;
    obj["type"] = "BaseComponent"; // childrens will override this
    obj["x"] = scenePos().x();
    obj["y"] = scenePos().y();
    obj["name"] = m_name;

    return obj;
}
} // namespace Atlas