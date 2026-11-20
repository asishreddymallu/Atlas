#pragma once
#include <QGraphicsPathItem>
#include <QPainterPath>
#include "ui/theme/theme_manager.h"

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
    /// The name drawn inside the component ("" when it has none) and the
    /// scene rectangle its text covers (T9).
    QString insideName() const { return m_label ? m_name : QString(); }
    QRectF  insideNameRect() const;
    /// The colour of a name inside a component: the theme's diagram label
    /// colour, else black or white, whichever reads better on the fill (T9:
    /// the app's text colour read at 1.4:1 on the grey in the dark themes).
    static QColor nameColorFor(const DiagramColors &colors);

  private:
    QGraphicsTextItem *m_label = nullptr;
    QString m_name;
};
} // namespace Kites