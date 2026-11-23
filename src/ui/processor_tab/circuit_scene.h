#pragma once
#include "ui/theme/theme_manager.h"

#include "processor_designs/components/base_component.h"
#include "processor_designs/components/wire_item.h"
#include "processor/circuit_stage.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QPainterPath>
#include <QPointF>
#include <QTableWidget>
#include <filesystem>


namespace Atlas
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

    /// T9: one per component, the name it shows. `caption` names are drawn
    /// outside the component (it has no name inside); `rect` is the scene
    /// rectangle the name covers.
    struct ComponentLabel
    {
        QGraphicsItem *component = nullptr;
        QString        text;
        QRectF         rect;
        bool           caption = false;
        bool           visible = false;
    };
    QList<ComponentLabel> componentLabels() const;
    /// The legend (nullptr before a circuit is loaded) and its entry count.
    QGraphicsItem *legendItem() const { return m_legend; }
    /// The pipelines' stage headers (IF..WB, left to right) and their rects.
    QList<QPair<QString, QRectF>> stageHeaders() const;
    int            legendEntries() const { return m_legendEntries; }
    /// The colour of the names outside components and of the legend text:
    /// the theme's diagram label colour when it reads at 4.5:1 on the
    /// diagram's background, else white or black, whichever reads better.
    static QColor captionColorFor(const DiagramColors &colors);

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

    // T9: names, tooltips and the legend.
    struct Caption
    {
        BaseComponent *component = nullptr;
        QString        shortName, fullName;
    };
    QList<Caption>               m_captionSpecs;
    QList<QGraphicsRectItem *>   m_captions;
    QGraphicsRectItem           *m_legend        = nullptr;
    QList<QGraphicsItem *>       m_stageItems; ///< headers (QGraphicsRectItem chips) and divider lines
    int                          m_legendEntries = 0;
    bool                         m_pipelined     = false;
    void readComponentLabels(const QString &designFile, const QList<BaseComponent *> &components);
    void buildDecorations(); ///< (re)creates captions and legend in the theme's colours

    void setUpInstructionTable();
    void fitTableToCircuit(const QString &vmType);
  public slots:
    void updateCircuitState(const QList<QString> &wireList);
    /// The same, with each wire's stage and the stage table's cells.
    void updateCircuitSnapshot(const Atlas::CircuitSnapshot &snapshot);
    // maybe later we merge this two functions but for now this will have to do
};
} // namespace Atlas
