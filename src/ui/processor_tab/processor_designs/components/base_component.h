#pragma once
#include <QGraphicsPathItem>
#include <QPainterPath>

namespace Kites
{
class BaseComponent : public QGraphicsPathItem
{
  public:
    BaseComponent(const QPainterPath &path, const QString &name, QGraphicsItem *parent = nullptr);
    void setName(const QString &name);
    virtual QJsonObject toJson();
    /// Fill, outline and label in the current theme's diagram colours (T8).
    void applyThemeColors();

  private:
    QGraphicsTextItem *m_label = nullptr;
    QColor             m_defaultLabelColor; ///< the label's own colour, for themes that set none
    QString m_name;
};
} // namespace Kites