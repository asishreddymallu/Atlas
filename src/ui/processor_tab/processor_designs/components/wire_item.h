#pragma once

#include <QGraphicsPathItem>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPolygonF>

namespace Kites
{

class WireItem : public QGraphicsPathItem
{
  public:
    /// Off: not lit this cycle. Active: lit, drawn as always. Dimmed: lit,
    /// but for a stage other than the one selected from the editor - drawn
    /// in the lit colour at low opacity, so it stays visible and reads as
    /// secondary.
    enum class State
    {
        Off,
        Active,
        Dimmed
    };
    explicit WireItem(const QPainterPath &path, QGraphicsItem *parent = nullptr);
    void setName(const QString &name);
    QString getName();
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;
    QPainterPath shape() const override;
    // void updatePath(const QPainterPath& newPath);
    QRectF boundingRect() const override;
    void addJunction(const QPointF &scenePos);
    void popJunction();
    void addArrowHead(const QPolygonF &arroHead);
    void saveArrowHead();
    void setActive(bool isActive); ///< Active or Off; kept for existing callers
    void setState(State state);
    State state() const { return m_state; }
    /// Pens from the current theme's diagram colours (T8), keeping the state.
    void applyThemeColors();
    QJsonObject toJson();

  private:
    QString m_name;
    QList<QPointF> m_junctions; // these are all the point where we branch the wire
    QList<QPolygonF> m_arrowHeads;
    QPolygonF m_currentArrowHead;

    QPen m_activePen;
    QPen m_inactivePen;
    QPen m_dimmedPen;
    State m_state = State::Off;
};
} // namespace Kites