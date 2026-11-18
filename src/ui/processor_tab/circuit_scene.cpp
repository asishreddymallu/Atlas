#include "circuit_scene.h"
#include "processor_designs/components/alu_item.h"
#include "processor_designs/components/and_gate.h"
#include "processor_designs/components/base_component.h"
#include "processor_designs/components/hori_rect.h"
#include "processor_designs/components/mux_item.h"
#include "processor_designs/components/pipeline_reg.h"
#include "processor_designs/components/rect_item.h"
#include "processor_designs/components/short_alu.h"
#include "processor_designs/components/short_rect.h"
#include "processor_designs/components/wire_item.h"
#include <QFile>
#include <QGraphicsProxyWidget>
#include <QHeaderView>
#include <QIODevice>
#include <QInputDialog>
#include <QJsonArray>
#include <QLineEdit>
#include <QTimer>
#include <QJsonDocument>
#include <QHash>
#include "common/instructions.h"

namespace Kites
{

CircuitScene::CircuitScene(QObject *parent) : QGraphicsScene(parent)
{
    setSceneRect(-100, -400, 300, 500);
    // setSceneRect(-1000, -1000, 2000, 2000);
    setBackgroundBrush(Qt::black);
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(100); // flash for 100ms

    connect(m_timer, &QTimer::timeout, this,
            [this]()
            {
                if (m_stayActive)
                    return;
                m_wiresShowing = false; // applyWireStates turns every wire off
                applyWireStates();
                update();
            });

    setUpInstructionTable();
}

void CircuitScene::setUpInstructionTable()
{
    m_instructionTable = new QTableWidget();
    m_instructionTable->verticalHeader()->setVisible(false);
    m_instructionTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    // Add the table to the scene so fitTableToCircuit can locate and position it
    QGraphicsProxyWidget *proxy = addWidget(m_instructionTable);
    proxy->setZValue(1000); // keep table on top of circuit items
    proxy->setPos(0, 0);
    // fitTableToCircuit();
}

void CircuitScene::fitTableToCircuit(const QString &vmType)
{
    if (!m_instructionTable)
        return;

    // ── 1. Define columns based on vmType ──
    if (vmType == "pipeline")
    {
        m_columnKeys = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"};
    }
    else if (vmType == "single_cycle")
    {
        m_columnKeys = {"CI"};
    }
    else
    {
        m_columnKeys = {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"}; // default
    }

    // ── 2. Set up table structure ONCE ──
    m_instructionTable->clear();
    m_instructionTable->setColumnCount(m_columnKeys.size());
    m_instructionTable->setRowCount(1);
    m_instructionTable->setHorizontalHeaderLabels(m_columnKeys);
    m_instructionTable->verticalHeader()->setVisible(false);
    m_instructionTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_instructionTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_instructionTable->setFocusPolicy(Qt::NoFocus);

    // Bigger bold font for header
    QFont headerFont = m_instructionTable->horizontalHeader()->font();
    headerFont.setBold(true);
    headerFont.setPointSize(11);
    m_instructionTable->horizontalHeader()->setFont(headerFont);

    // Bigger bold font for cell values
    QFont cellFont = m_instructionTable->font();
    cellFont.setBold(true);
    cellFont.setPointSize(11);
    m_instructionTable->setFont(cellFont);

    // ── 3. Fit width to circuit bounds ──
    QRectF bounds;
    for (QGraphicsItem *item : items())
    {
        if (dynamic_cast<QGraphicsProxyWidget *>(item))
            continue;
        bounds = bounds.united(item->mapToScene(item->boundingRect()).boundingRect());
    }

    if (bounds.isEmpty() || bounds.width() == 0)
    {
        qWarning() << "fitTableToCircuit: empty bounds";
        return;
    }

    double colWidth = bounds.width() / m_columnKeys.size();
    for (int i = 0; i < m_columnKeys.size(); ++i)
        m_instructionTable->setColumnWidth(i, (int)colWidth);

    int totalWidth = (int)(colWidth * m_columnKeys.size());
    m_instructionTable->setFixedWidth(totalWidth + 4);
    m_instructionTable->setFixedHeight(m_instructionTable->horizontalHeader()->height() +
                                       m_instructionTable->rowHeight(0));

    // ── 4. Position above circuit ──
    for (QGraphicsItem *item : items())
    {
        QGraphicsProxyWidget *proxy = dynamic_cast<QGraphicsProxyWidget *>(item);
        if (proxy && proxy->widget() == m_instructionTable)
        {
            proxy->setPos(bounds.left(), bounds.top() - proxy->boundingRect().height() - 10);
            break;
        }
    }
}


void CircuitScene::mousePressEvent(QGraphicsSceneMouseEvent *mouseEvent)
{
    if (mouseEvent->button() == Qt::LeftButton)
    {
        // Right-click: Show context menu
        qInfo() << "Mouse pressed at:" << mouseEvent->scenePos();
    }
    else
    {
        // For other mouse buttons, call the base implementation
        QGraphicsScene::mousePressEvent(mouseEvent);
    }
}

void CircuitScene::updateCircuitState(const QList<QString> &wireList)
{
    // Names only, so no stages: with a stage selected every lit wire is
    // drawn dimmed, and with none selected exactly as before.
    m_litWires = wireList;
    m_litStages.clear();
    m_wiresShowing = true;
    applyWireStates();

    // Request a redraw of the scene to reflect changes
    update();
    m_timer->start(); // restart the timer to turn off the wires after interval
}

void CircuitScene::updateCircuitSnapshot(const Kites::CircuitSnapshot &snapshot)
{
    m_cells = snapshot.cells;
    fillInstructionTable();

    m_litWires     = snapshot.wires;
    m_litStages    = snapshot.stages;
    m_wiresShowing = true;
    applyWireStates();

    update();
    m_timer->start(); // restart the timer to turn off the wires after interval
}

void CircuitScene::setSelectedStages(quint8 stages)
{
    m_selectedStages = stages;
    // Restyle only what is showing: once the off-timer has turned the wires
    // off, a cursor move does not relight them.
    if (m_wiresShowing)
        applyWireStates();
    fillInstructionTable();
    update();
}

QColor CircuitScene::selectedColumnColor()
{
    // The lit-wire yellow, translucent, so the column reads as the same
    // thing the emphasised wires are.
    return QColor(255, 255, 0, 70);
}

void CircuitScene::applyWireStates()
{
    QHash<QString, quint8> stageOf;
    for (qsizetype i = 0; i < m_litWires.size(); ++i)
        stageOf[m_litWires[i]] |= (i < m_litStages.size() ? m_litStages[i] : kNoStage);

    for (QGraphicsItem *item : items())
    {
        WireItem *wire = dynamic_cast<WireItem *>(item);
        if (!wire)
            continue;
        const auto it = stageOf.constFind(wire->getName());
        if (!m_wiresShowing || it == stageOf.constEnd())
            wire->setState(WireItem::State::Off);
        else if (m_selectedStages == kNoStage || (*it & m_selectedStages))
            wire->setState(WireItem::State::Active);
        else
            wire->setState(WireItem::State::Dimmed);
    }
}

void CircuitScene::fillInstructionTable()
{
    if (!m_instructionTable || m_instructionTable->rowCount() < 1)
        return;

    // Pipelined: columns IF/ID, ID/EX, EX/MEM, MEM/WB hold the instructions
    // labelled ID, EX, MEM and WB. Single-cycle: one CI column, no stage.
    const bool pipelined = m_columnKeys.size() == 4;
    for (int column = 0; column < m_instructionTable->columnCount(); ++column)
    {
        const StageCell cell = column < m_cells.size() ? m_cells[column] : StageCell{};
        auto *item = new QTableWidgetItem();
        item->setTextAlignment(Qt::AlignCenter);

        const bool hasPc = cell.pc != UINT64_MAX;
        const QString pcText =
            hasPc ? QStringLiteral("PC 0x%1").arg(cell.pc, 8, 16, QLatin1Char('0')) : QString();
        if (cell.instruction == 0x00000013 || !hasPc) // NOP: a bubble, or nothing yet
        {
            item->setText(QStringLiteral("bubble"));
            item->setForeground(QColor(128, 128, 128));
            item->setToolTip(hasPc ? QStringLiteral("bubble (nop) at ") + pcText
                                   : QStringLiteral("bubble"));
        }
        else
        {
            item->setText(QString::fromStdString(instruction_set::disassemble(cell.instruction)));
            item->setToolTip(pcText);
        }

        const quint8 stage = pipelined ? stageBit(static_cast<WireStage>(column + 1)) : kNoStage;
        if (stage & m_selectedStages)
            item->setBackground(selectedColumnColor());

        m_instructionTable->setItem(0, column, item);
    }
}

void CircuitScene::loadScene(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "Could not open file:" << file.errorString();
        return;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(fileData);
    if (doc.isNull())
    {
        qWarning() << "Failed to parse JSON file.";
        return;
    }

    QJsonObject sceneJson = doc.object();

    // 2. Clear the current scene
    clear(); // Deletes all items from the scene

    // clear() deletes all items, but we also need to reset the instruction table

    // 3. Load Components (The "Factory")
    if (sceneJson.contains("components") && sceneJson["components"].isArray())
    {
        QJsonArray componentsArray = sceneJson["components"].toArray();
        for (const QJsonValue &compValue : componentsArray)
        {
            QJsonObject compObj = compValue.toObject();
            QString type = compObj["type"].toString();
            QString name = compObj["name"].toString();

            BaseComponent *component = nullptr;

            // --- Your Factory ---
            if (type == "ALUItem")
            {
                component = new ALUItem();
            }
            else if (type == "RectItem")
            {
                component = new RectItem();
            }
            else if (type == "HoriRect")
            {
                component = new HoriRect();
            }
            else if (type == "MuxItem")
            {
                component = new MuxItem();
            }
            else if (type == "PipelineReg")
            {
                component = new PipelineReg();
            }
            else if (type == "SALUItem")
            {
                component = new SALUItem();
            }
            else if (type == "ShortRect")
            {
                component = new ShortRect();
            }
            else if (type == "AndGateItem")
            {
                component = new AndGateItem();
            }
            else
            {
                // Unknown type
                qWarning() << "Unknown component type dropped:" << type;
                // event->ignore();
            }

            if (component)
            {
                // Read position
                double x = compObj["x"].toDouble();
                double y = compObj["y"].toDouble();
                component->setPos(x, y);

                // Read name
                if (compObj.contains("name"))
                {
                    component->setName(compObj["name"].toString());
                }

                addItem(component);
            }
        }
    }

    // 4. Load Wires
    if (sceneJson.contains("wires") && sceneJson["wires"].isArray())
    {
        QJsonArray wiresArray = sceneJson["wires"].toArray();
        for (const QJsonValue &wireValue : wiresArray)
        {
            QJsonObject wireObj = wireValue.toObject();

            // Re-create the path
            QJsonArray pathArray = wireObj["path"].toArray();
            QPainterPath path;
            for (int i = 0; i < pathArray.size(); ++i)
            {
                QJsonObject pointObj = pathArray[i].toObject();
                double x = pointObj["x"].toDouble();
                double y = pointObj["y"].toDouble();

                int type = pointObj["type"].toInt();

                if (type == QPainterPath::MoveToElement)
                {
                    path.moveTo(x, y); // This starts a new branch
                }
                else if (type == QPainterPath::LineToElement)
                {
                    path.lineTo(x, y); // This continues a branch
                }
            }

            // Create the wire
            WireItem *wire = new WireItem(path);
            // wire->setPen(QPen(Qt::white, 2, Qt::SolidLine));

            // Set the name
            if (wireObj.contains("name"))
            {
                wire->setName(wireObj["name"].toString());
            }

            // Re-create the junction dots
            if (wireObj.contains("junctions") && wireObj["junctions"].isArray())
            {
                QJsonArray junctionsArray = wireObj["junctions"].toArray();
                QList<QPointF> junctionPoints;
                for (const QJsonValue &jval : junctionsArray)
                {
                    QJsonObject p = jval.toObject();
                    // junctionPoints.append(QPointF(p["x"].toDouble(), p["y"].toDouble()));
                    wire->addJunction(QPointF(p["x"].toDouble(), p["y"].toDouble()));
                }
                // wire->setJunctions(junctionPoints);
            }

            if (wireObj.contains("arrowHeads") && wireObj["arrowHeads"].isArray())
            {
                // QList<QPolygonF> allPolygons;
                QJsonArray arrowHeadArray = wireObj["arrowHeads"].toArray();
                for (const QJsonValue &polyVal : arrowHeadArray)
                {
                    QPolygonF polygon; // Create a new QPolygonF
                    QJsonArray polygonPointArray = polyVal.toArray();
                    for (const QJsonValue &pointVal : polygonPointArray)
                    {
                        QJsonObject p = pointVal.toObject();
                        polygon.append(QPointF(p["x"].toDouble(), p["y"].toDouble()));
                    }
                    wire->addArrowHead(polygon);
                    // allPolygons.append(polygon);
                }
            }
            addItem(wire);
        }
    }

    setUpInstructionTable();

    // this is very very hacky but it works fow now
    //  i am sooo sorrry
    if (fileName.contains("single_cycle"))
    {
        fitTableToCircuit("single_cycle");
    }
    else
    {
        fitTableToCircuit("pipeline");
    }
    fillInstructionTable(); // every cell reads "bubble" until the first step
}
} // namespace Kites
