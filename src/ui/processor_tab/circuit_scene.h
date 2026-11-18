#pragma once

#include "processor_designs/components/base_component.h"
#include "processor_designs/components/wire_item.h"
#include "processor/circuit_stage.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPointF>
#include <QTableWidget>
#include <filesystem>


namespace Kites
{
class CircuitScene : public QGraphicsScene
{
    Q_OBJECT
  public:
    explicit CircuitScene(QObject *parent = nullptr);

    void handleMouseMove(const QPointF &scenePos);

    void loadScene(const QString &fileName);
    void setWireStayActive(bool stayActive)
    {
        m_stayActive = stayActive;
    }
    bool getWireStayActive() const
    {
        return m_stayActive;
    }

    /// The stages selected from the editor's cursor line (stageBit mask).
    /// 0 - nothing selected - draws every lit wire exactly as before. Any
    /// other mask draws the lit wires of those stages normally and every
    /// other lit wire, staged or backbone, dimmed, and highlights those
    /// stages' table columns. It only restyles wires that are showing: once
    /// the off-timer has turned them off (Wires Stay Active off), a new
    /// selection relights nothing.
    void setSelectedStages(quint8 stages);
    quint8 selectedStages() const
    {
        return m_selectedStages;
    }
    /// True from an update until the off-timer turns the wires off.
    bool wiresShowing() const
    {
        return m_wiresShowing;
    }
    QTableWidget *instructionTable() const
    {
        return m_instructionTable;
    }
    /// The colour a selected column's cell is filled with.
    static QColor selectedColumnColor();

  protected:
    // void mouseMoveEvent(QGraphicsSceneMouseEvent *mouseEvent) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent) override;

  private:
    std::filesystem::path m_sceneFilePath; // unused variable

    QTimer *m_timer;                            // well use it to make the wire blink
    QTableWidget *m_instructionTable = nullptr; // the table that shows the instructions
    bool m_stayActive = false;                  // whether to keep the wires active or not
    QStringList m_columnKeys;

    QList<QString> m_litWires;            // the last update's lit wires
    QList<quint8>  m_litStages;           // their stage masks, same length (or empty)
    QList<StageCell> m_cells;             // the last update's table cells
    quint8 m_selectedStages = kNoStage;
    bool   m_wiresShowing   = false;

    void applyWireStates();
    void fillInstructionTable();

    void setUpInstructionTable();
    void fitTableToCircuit(const QString &vmType);
  public slots:
    void updateCircuitState(const QList<QString> &wireList);
    /// The same, with each wire's stage and the stage table's cells.
    void updateCircuitSnapshot(const Kites::CircuitSnapshot &snapshot);
    // maybe later we merge this two functions but for now this will have to do
};
} // namespace Kites
