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
#include "ui/theme/theme_manager.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPathStroker>
#include <algorithm>
#include <cmath>
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

namespace Atlas
{

CircuitScene::CircuitScene(QObject *parent) : QGraphicsScene(parent)
{
    setSceneRect(-100, -400, 300, 500);
    // setSceneRect(-1000, -1000, 2000, 2000);
    setBackgroundBrush(ThemeManager::getInstance().diagramColors().background);
    // T8: the diagram follows the theme (the house themes keep it black).
    connect(&ThemeManager::getInstance(), &ThemeManager::themeChangedSignal, this, [this]() {
        setBackgroundBrush(ThemeManager::getInstance().diagramColors().background);
        for (QGraphicsItem *item : items())
        {
            if (auto *wire = dynamic_cast<WireItem *>(item))
                wire->applyThemeColors();
            else if (auto *component = dynamic_cast<BaseComponent *>(item))
                component->applyThemeColors();
        }
        styleInstructionTable();
        fillInstructionTable();
        buildDecorations(); // names and legend in the new theme's colours
        update();
    });
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
    styleInstructionTable();
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

void CircuitScene::updateCircuitSnapshot(const Atlas::CircuitSnapshot &snapshot)
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
    // The lit-wire colour (yellow by default), translucent, so the column
    // reads as the same thing the emphasised wires are.
    QColor c = ThemeManager::getInstance().diagramColors().wireActive;
    c.setAlpha(70);
    return c;
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
    m_captions.clear(); // clear() deleted them (and the legend, and the stage headers)
    m_legend = nullptr;
    m_stageItems.clear();
    m_captionSpecs.clear();
    QList<BaseComponent *> created; // in the design's order, nullptr for a dropped type

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

            created << component;
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

    // T9: a name on every component, a tooltip, and a legend. After the
    // table is placed, so the names keep clear of it.
    m_pipelined = !fileName.contains("single_cycle");
    readComponentLabels(fileName, created);
    buildDecorations();
}

void CircuitScene::readComponentLabels(const QString &designFile, const QList<BaseComponent *> &components)
{
    // component_labels.json sits beside the designs: per design, one entry
    // per component in the design's order ({type, label, title}).
    const QFileInfo info(designFile);
    QFile           file(info.dir().filePath("component_labels.json"));
    if (!file.open(QIODevice::ReadOnly))
    {
        qWarning() << "No component labels for" << designFile;
        return;
    }
    const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(info.fileName()).toArray();
    for (int i = 0; i < components.size() && i < entries.size(); ++i)
    {
        BaseComponent    *component = components[i];
        const QJsonObject e         = entries[i].toObject();
        if (!component)
            continue;
        QString       shortName = e.value("label").toString();
        const QString fullName  = e.value("title").toString();
        // One rule for every label (final/polish): a name the design draws
        // inside its box (Instr Mem, Reg File, Data Mem, HDU, Fwd Unit, and
        // Control on the single-cycle diagram) becomes a caption like PC, Mux
        // or ALU - same font, colour and placement - instead of larger text
        // that overflowed the box.
        const QString inside = component->takeInsideName();
        if (shortName.isEmpty())
            shortName = inside;
        component->setToolTip(fullName.isEmpty() ? shortName : fullName);
        if (!shortName.isEmpty())
            m_captionSpecs.push_back({component, shortName, fullName});
    }
}

void CircuitScene::styleInstructionTable()
{
    if (!m_instructionTable)
        return;
    const DiagramColors d    = ThemeManager::getInstance().diagramColors();
    const QColor        ink  = captionColorFor(d);
    // The header a step lighter (or darker) than the diagram, towards the ink.
    const QColor header = QColor::fromRgbF(d.background.redF() * 0.88 + ink.redF() * 0.12,
                                           d.background.greenF() * 0.88 + ink.greenF() * 0.12,
                                           d.background.blueF() * 0.88 + ink.blueF() * 0.12);
    m_instructionTable->setStyleSheet(
        QStringLiteral("QTableWidget { background: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
                       "QHeaderView::section { background: %4; color: %2; border: none;"
                       " border-bottom: 1px solid %3; }")
            .arg(d.background.name(), ink.name(), d.wire.name(), header.name()));
}

QColor CircuitScene::captionColorFor(const DiagramColors &colors)
{
    auto contrast = [](const QColor &a, const QColor &b) {
        auto lum = [](const QColor &c) {
            auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
            return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
        };
        const double la = lum(a), lb = lum(b);
        return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
    };
    if (colors.label.isValid() && contrast(colors.label, colors.background) >= 4.5)
        return colors.label;
    const QColor white(Qt::white), black(Qt::black);
    return contrast(white, colors.background) >= contrast(black, colors.background) ? white : black;
}

namespace
{
/// A name or legend "chip": the diagram's background behind the text, so a
/// wire running underneath does not cross the letters.
QGraphicsRectItem *makeChip(const QRectF &rect, const QColor &background)
{
    auto *chip = new QGraphicsRectItem(rect);
    chip->setBrush(background);
    chip->setPen(Qt::NoPen);
    chip->setZValue(500); // over wires and components, under the table (1000)
    return chip;
}
} // namespace

void CircuitScene::buildDecorations()
{
    for (QGraphicsRectItem *c : m_captions)
        delete c;
    m_captions.clear();
    delete m_legend;
    m_legend        = nullptr;
    m_legendEntries = 0;
    for (QGraphicsItem *i : m_stageItems)
        delete i;
    m_stageItems.clear();

    const DiagramColors colors = ThemeManager::getInstance().diagramColors();
    const QColor        ink    = captionColorFor(colors);
    QFont               font;
    font.setPointSizeF(8.5);
    const QFontMetricsF fm(font);

    // What a name may not cover: components, the names inside them, the
    // instruction table, and the names already placed.
    QList<QRectF> taken;
    QRectF        circuit;
    for (QGraphicsItem *item : items())
    {
        if (auto *c = dynamic_cast<BaseComponent *>(item))
        {
            const QRectF r = c->mapRectToScene(c->boundingRect());
            taken << r;
            circuit = circuit.united(r);
            if (!c->insideName().isEmpty())
                taken << c->insideNameRect();
        }
        else if (dynamic_cast<QGraphicsProxyWidget *>(item))
            taken << item->sceneBoundingRect();
    }
    auto isFree = [&](const QRectF &r) {
        for (const QRectF &t : taken)
            if (t.intersects(r))
                return false;
        return true;
    };
    // The wires, as scene shapes: a name is placed off every wire if it can
    // be, so its chip never hides a wire; only when no such place exists
    // does it fall back to a place that is merely off the components.
    // The stroke, not the path: QPainterPath::intersects treats an open
    // polyline as the area it encloses.
    QList<QPainterPath> wirePaths;
    QPainterPathStroker stroker;
    stroker.setWidth(2);
    for (QGraphicsItem *item : items())
        if (auto *w = dynamic_cast<WireItem *>(item))
            wirePaths << stroker.createStroke(w->mapToScene(w->path()));
    auto crossesWire = [&](const QRectF &r) {
        for (const QPainterPath &p : wirePaths)
            if (p.intersects(r))
                return true;
        return false;
    };

    // Names: just outside the component, the first free place among above,
    // below, right and left, then the corners, then a little further out.
    constexpr qreal pad = 2;
    for (const Caption &spec : m_captionSpecs)
    {
        const QRectF comp = spec.component->mapRectToScene(spec.component->boundingRect());
        const QSizeF size(fm.horizontalAdvance(spec.shortName) + 2 * pad, fm.height());
        QRectF       chosen;
        for (const bool avoidWires : {true, false})
        for (qreal gap : {2.0, 6.0, 12.0, 20.0, 30.0, 42.0, 55.0})
        {
            if (!chosen.isNull())
                break; // found in the wire-free pass
            const qreal w = size.width(), h = size.height();
            const QPointF candidates[] = {
                {comp.center().x() - w / 2, comp.top() - h - gap},  {comp.center().x() - w / 2, comp.bottom() + gap},
                {comp.left(), comp.top() - h - gap},                {comp.right() - w, comp.top() - h - gap},
                {comp.right() + gap, comp.center().y() - h / 2},    {comp.left() - gap - w, comp.center().y() - h / 2},
                {comp.right() + gap, comp.top() - h - gap},         {comp.left() - gap - w, comp.top() - h - gap},
                {comp.right() + gap, comp.bottom() + gap},          {comp.left() - gap - w, comp.bottom() + gap},
            };
            // A pipeline register's name always goes above its bar, so the
            // four line up under the stage headers like a row.
            const bool register_ = dynamic_cast<PipelineReg *>(spec.component) != nullptr;
            for (const QPointF &p : candidates)
                if (register_ && p != candidates[0])
                    continue;
                else if (isFree(QRectF(p, size)) && !(avoidWires && crossesWire(QRectF(p, size))))
                {
                    chosen = QRectF(p, size);
                    break;
                }
            if (!chosen.isNull())
                break;
        }
        if (chosen.isNull()) // nowhere free: above it, and the test says so
            chosen = QRectF(QPointF(comp.center().x() - size.width() / 2, comp.top() - size.height() - 2), size);
        auto *chip = makeChip(chosen, colors.background);
        auto *text = new QGraphicsSimpleTextItem(spec.shortName, chip);
        text->setFont(font);
        text->setBrush(ink);
        text->setPos(chosen.left() + pad, chosen.top());
        chip->setToolTip(spec.fullName);
        chip->setData(0, spec.shortName);
        addItem(chip);
        m_captions << chip;
        taken << chosen;
    }

    // The legend: wire states (and, on the pipelines, the dimmed wires of
    // other stages and the bubble cells), in a free corner of the circuit.
    struct Entry
    {
        QPen    pen;
        QString text;
        QColor  textColor;
        bool    line;
    };
    QColor dimmed = colors.wireActive;
    dimmed.setAlpha(90);
    QList<Entry> entries = {{QPen(colors.wireActive, 2), "active wire", ink, true},
                            {QPen(colors.wire, 1), "inactive wire", ink, true}};
    if (m_pipelined)
    {
        entries.push_back({QPen(dimmed, 2), "active, other stage", ink, true});
        entries.push_back({QPen(), "bubble = stalled or flushed", ink, false});
    }
    const qreal sample = 22, rowH = fm.height() + 2;
    qreal       textW  = 0;
    for (const Entry &e : entries)
        textW = std::max(textW, fm.horizontalAdvance(e.text));
    const QSizeF lsize(sample + 6 + textW + 2 * 4, entries.size() * rowH + 2 * 3);
    const qreal  m = 8;
    // Top corners first: the view shows the top of the circuit at any size.
    const QPointF corners[] = {{circuit.left(), circuit.top()},
                               {circuit.right() - lsize.width(), circuit.top()},
                               {circuit.left(), circuit.bottom() - lsize.height()},
                               {circuit.right() - lsize.width(), circuit.bottom() - lsize.height()},
                               {circuit.left(), circuit.bottom() + m}}; // below the circuit: always free
    QRectF lrect;
    for (const QPointF &p : corners)
        if (isFree(QRectF(p, lsize)))
        {
            lrect = QRectF(p, lsize);
            break;
        }
    if (lrect.isNull())
        lrect = QRectF(corners[4], lsize);
    m_legend = makeChip(lrect, colors.background);
    m_legend->setPen(QPen(colors.wire, 1));
    m_legend->setToolTip("Legend");
    qreal y = lrect.top() + 3;
    for (const Entry &e : entries)
    {
        const qreal mid = y + rowH / 2;
        if (e.line)
        {
            auto *line = new QGraphicsLineItem(lrect.left() + 4, mid, lrect.left() + 4 + sample, mid, m_legend);
            line->setPen(e.pen);
        }
        else
        {
            auto *b = new QGraphicsSimpleTextItem("bubble", m_legend);
            QFont small = font;
            small.setPointSizeF(6);
            b->setFont(small);
            b->setBrush(QColor(128, 128, 128)); // the table's bubble grey
            b->setPos(lrect.left() + 4, mid - QFontMetricsF(small).height() / 2);
        }
        auto *t = new QGraphicsSimpleTextItem(e.line ? e.text : e.text.mid(e.text.indexOf('=') + 2), m_legend);
        t->setFont(font);
        t->setBrush(e.textColor);
        t->setPos(lrect.left() + 4 + sample + 6, mid - fm.height() / 2);
        y += rowH;
    }
    m_legendEntries = static_cast<int>(entries.size());
    addItem(m_legend);

    // Stage headers (pipelines): IF, ID, EX, MEM, WB over the columns the
    // pipeline registers bound, above the instruction table, with faint
    // dividers down each register's centre.
    if (!m_pipelined)
        return;
    QList<QRectF> regs;
    QRectF        table;
    for (QGraphicsItem *item : items())
    {
        if (auto *c = dynamic_cast<BaseComponent *>(item); c && c->boundingRect().height() >= 400)
            regs << c->mapRectToScene(c->boundingRect()); // the 20 x 500 pipeline registers
        else if (dynamic_cast<QGraphicsProxyWidget *>(item))
            table = item->sceneBoundingRect();
    }
    if (regs.size() != 4)
        return;
    std::sort(regs.begin(), regs.end(), [](const QRectF &a, const QRectF &b) { return a.left() < b.left(); });
    const qreal edges[] = {circuit.left(), regs[0].center().x(), regs[1].center().x(), regs[2].center().x(),
                           regs[3].center().x(), circuit.right()};
    const char *names[] = {"IF", "ID", "EX", "MEM", "WB"};
    QFont       headerFont = font;
    headerFont.setBold(true);
    headerFont.setPointSizeF(10);
    const QFontMetricsF hm(headerFont);
    const qreal         top = (table.isNull() ? circuit.top() : table.top()) - hm.height() - 6;
    QColor              faint = colors.wire;
    faint.setAlpha(110);
    for (int s = 0; s < 5; ++s)
    {
        const qreal  w = hm.horizontalAdvance(names[s]) + 8;
        const QRectF r((edges[s] + edges[s + 1]) / 2 - w / 2, top, w, hm.height());
        auto        *chip = makeChip(r, colors.background);
        auto        *t    = new QGraphicsSimpleTextItem(names[s], chip);
        t->setFont(headerFont);
        t->setBrush(ink);
        t->setPos(r.left() + 4, r.top());
        chip->setData(0, QString::fromLatin1(names[s]));
        chip->setToolTip(QStringLiteral("%1 stage").arg(names[s]));
        addItem(chip);
        m_stageItems << chip;
    }
    for (const QRectF &reg : regs)
    {
        auto *line = new QGraphicsLineItem(reg.center().x(), top, reg.center().x(), reg.top());
        line->setPen(QPen(faint, 1, Qt::DashLine));
        line->setZValue(-1); // under everything
        addItem(line);
        m_stageItems << line;
    }
}

QList<QPair<QString, QRectF>> CircuitScene::stageHeaders() const
{
    QList<QPair<QString, QRectF>> out;
    for (QGraphicsItem *i : m_stageItems)
        if (auto *chip = dynamic_cast<QGraphicsRectItem *>(i))
            out.push_back({chip->data(0).toString(), chip->sceneBoundingRect()});
    return out;
}

QList<CircuitScene::ComponentLabel> CircuitScene::componentLabels() const
{
    QList<ComponentLabel> out;
    for (QGraphicsItem *item : items())
    {
        auto *c = dynamic_cast<BaseComponent *>(item);
        if (!c)
            continue;
        ComponentLabel l;
        l.component = c;
        if (!c->insideName().isEmpty())
        {
            l.text    = c->insideName();
            l.rect    = c->insideNameRect();
            l.visible = true;
        }
        else
            for (qsizetype i = 0; i < m_captionSpecs.size() && i < m_captions.size(); ++i)
                if (m_captionSpecs[i].component == c) // captions are made in spec order
                {
                    l.text    = m_captionSpecs[i].shortName;
                    l.rect    = m_captions[i]->sceneBoundingRect();
                    l.caption = true;
                    l.visible = m_captions[i]->isVisible();
                }
        out << l;
    }
    return out;
}
} // namespace Atlas
