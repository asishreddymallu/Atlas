/**
 * @file circuit_label_rule_test.cpp
 * @brief final/polish: one rule for every component label on the five
 *        processor diagrams - same font and colour, a caption outside the
 *        box (Instr Mem, Reg File, Data Mem, HDU and Fwd Unit included),
 *        never clipped, off the wires wherever a wire-free place exists -
 *        and the instruction row in the diagram's colours.
 */
#include "ui/processor_tab/circuit_scene.h"
#include "ui/processor_tab/processor_designs/components/base_component.h"
#include "ui/processor_tab/processor_designs/components/pipeline_reg.h"
#include "ui/processor_tab/processor_designs/components/wire_item.h"
#include "ui/theme/theme_manager.h"

#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPathStroker>
#include <QTableWidget>

#include <gtest/gtest.h>

#include <memory>
#include <set>

using namespace Atlas;

namespace
{
struct Design
{
    const char *processor;
    const char *file;
};
const Design kDesigns[] = {{"rvss", "single_cycle_draft1.json"},
                           {"nh_nf", "NH_NF_Processor.json"},
                           {"h_nf", "H_NF_Processor.json"},
                           {"nh_f", "NH_F_Processor.json"},
                           {"h_f", "H_F_Processor.json"}};

std::unique_ptr<CircuitScene> load(const Design &d)
{
    auto scene = std::make_unique<CircuitScene>();
    scene->loadScene(QStringLiteral("../resources/circuit_designs/") + d.file);
    return scene;
}

/// The caption chips (not the IF..WB stage headers), with their text.
QList<QPair<QGraphicsRectItem *, QGraphicsSimpleTextItem *>> captions(CircuitScene &scene)
{
    static const std::set<QString> stages = {"IF", "ID", "EX", "MEM", "WB"};
    QList<QPair<QGraphicsRectItem *, QGraphicsSimpleTextItem *>> out;
    for (QGraphicsItem *item : scene.items())
    {
        auto *chip = dynamic_cast<QGraphicsRectItem *>(item);
        if (!chip || chip->data(0).toString().isEmpty() || stages.count(chip->data(0).toString()))
            continue;
        for (QGraphicsItem *child : chip->childItems())
            if (auto *text = dynamic_cast<QGraphicsSimpleTextItem *>(child))
                out << qMakePair(chip, text);
    }
    return out;
}

bool crossesAWire(CircuitScene &scene, const QRectF &rect)
{
    QPainterPathStroker stroker;
    stroker.setWidth(2);
    for (QGraphicsItem *item : scene.items())
        if (auto *w = dynamic_cast<WireItem *>(item))
            if (stroker.createStroke(w->mapToScene(w->path())).intersects(rect))
                return true;
    return false;
}

class CircuitLabelRule : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(CircuitLabelRule, NoComponentDrawsItsNameInsideItsBox)
{
    const Design &d     = kDesigns[GetParam()];
    auto          scene = load(d);
    for (QGraphicsItem *item : scene->items())
        if (auto *c = dynamic_cast<BaseComponent *>(item))
            EXPECT_TRUE(c->insideName().isEmpty()) << d.file << ": " << c->toolTip().toStdString();
    for (const CircuitScene::ComponentLabel &l : scene->componentLabels())
        EXPECT_TRUE(l.caption) << d.file << ": " << l.text.toStdString();
}

TEST_P(CircuitLabelRule, EveryCaptionHasTheSameFontAndColourAndIsNotClipped)
{
    const Design &d     = kDesigns[GetParam()];
    auto          scene = load(d);
    const auto    all   = captions(*scene);
    ASSERT_GT(all.size(), 10) << d.file;
    const QFont  font  = all.first().second->font();
    const QColor ink   = all.first().second->brush().color();
    EXPECT_DOUBLE_EQ(font.pointSizeF(), 8.5);
    EXPECT_FALSE(font.bold());
    EXPECT_EQ(ink, CircuitScene::captionColorFor(ThemeManager::getInstance().diagramColors()));
    for (const auto &[chip, text] : all)
    {
        const std::string name = text->text().toStdString();
        EXPECT_EQ(text->font(), font) << d.file << ": " << name;
        EXPECT_EQ(text->brush().color(), ink) << d.file << ": " << name;
        // Not clipped: the text's whole ink is inside its chip.
        const QRectF ink2 = text->mapRectToScene(text->boundingRect());
        EXPECT_TRUE(chip->sceneBoundingRect().adjusted(-0.5, -0.5, 0.5, 0.5).contains(ink2)) << d.file << ": " << name;
    }
}

TEST_P(CircuitLabelRule, TheBigBoxesAreNamedOutsideAndBesideThem)
{
    const Design &d     = kDesigns[GetParam()];
    auto          scene = load(d);
    int           found = 0;
    for (const CircuitScene::ComponentLabel &l : scene->componentLabels())
    {
        if (l.text != "Instr Mem" && l.text != "Reg File" && l.text != "Data Mem" && l.text != "HDU" &&
            l.text != "Fwd Unit")
            continue;
        ++found;
        const QRectF box = l.component->mapRectToScene(l.component->boundingRect());
        EXPECT_FALSE(l.rect.intersects(box)) << d.file << ": " << l.text.toStdString() << " is inside its box";
        // Beside it: within the farthest placement step of an edge.
        EXPECT_TRUE(box.adjusted(-80, -70, 80, 70).contains(l.rect)) << d.file << ": " << l.text.toStdString();
    }
    EXPECT_GE(found, 3) << d.file;
}

TEST_P(CircuitLabelRule, PipelineRegisterNamesSitAboveTheirBars)
{
    const Design &d = kDesigns[GetParam()];
    if (QString(d.processor) == "rvss")
        GTEST_SKIP() << "single-cycle: no pipeline registers";
    auto scene = load(d);
    int  regs  = 0;
    for (const CircuitScene::ComponentLabel &l : scene->componentLabels())
        if (dynamic_cast<PipelineReg *>(l.component))
        {
            ++regs;
            EXPECT_LE(l.rect.bottom(), l.component->mapRectToScene(l.component->boundingRect()).top())
                << d.file << ": " << l.text.toStdString();
        }
    EXPECT_EQ(regs, 4) << d.file;
}

TEST(CircuitLabelRule, OnlyBoxedInNamesFallBackOntoAWire)
{
    // A name goes on the first wire-free place; only where none exists near
    // the box does it sit on a wire (on its chip, so the text stays legible).
    // These are those places, pinned so a new crossing is noticed.
    std::set<std::string> crossing;
    for (const Design &d : kDesigns)
    {
        auto scene = load(d);
        for (const CircuitScene::ComponentLabel &l : scene->componentLabels())
            if (crossesAWire(*scene, l.rect))
                crossing.insert(std::string(d.processor) + " " + l.text.toStdString());
    }
    EXPECT_EQ(crossing, (std::set<std::string>{"nh_nf Data Mem", "h_nf Data Mem", "h_nf IF/ID", "h_f IF/ID"}));
}

TEST(CircuitLabelRule, TheInstructionRowUsesTheDiagramsColours)
{
    auto scene = load(kDesigns[4]);
    const DiagramColors d = ThemeManager::getInstance().diagramColors();
    const QString       style = scene->instructionTable()->styleSheet();
    EXPECT_TRUE(style.contains(d.background.name())) << style.toStdString();
    EXPECT_TRUE(style.contains(CircuitScene::captionColorFor(d).name())) << style.toStdString();
}

INSTANTIATE_TEST_SUITE_P(Processors, CircuitLabelRule, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kDesigns[info.param].processor);
                         });
