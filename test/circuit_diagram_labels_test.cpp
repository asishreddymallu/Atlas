/**
 * @file circuit_diagram_labels_test.cpp
 * @brief T9: every component of the five processor diagrams is named, the
 *        names and the legend do not overlap, and they are readable.
 *
 * kites_test links no Qt resources, so the circuits are loaded from the
 * source tree (ctest runs in test/), the same files the app compiles in.
 */
#include "processor/processor_manager.h"
#include "ui/processor_tab/circuit_scene.h"
#include "ui/processor_tab/processor_designs/components/base_component.h"
#include "ui/theme/theme_manager.h"
#include "ui/timing_grid/timing_grid.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsProxyWidget>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

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

class CircuitDiagram : public ::testing::TestWithParam<size_t>
{
};
} // namespace

TEST_P(CircuitDiagram, EveryComponentHasAVisibleName)
{
    const Design &d     = kDesigns[GetParam()];
    auto          scene = load(d);
    int           components = 0;
    for (QGraphicsItem *item : scene->items())
        if (dynamic_cast<BaseComponent *>(item))
            ++components;
    ASSERT_GT(components, 10) << d.file;
    const QList<CircuitScene::ComponentLabel> labels = scene->componentLabels();
    EXPECT_EQ(labels.size(), components) << d.file;
    for (const CircuitScene::ComponentLabel &l : labels)
    {
        EXPECT_FALSE(l.text.trimmed().isEmpty()) << d.file;
        EXPECT_TRUE(l.visible) << d.file << " " << l.text.toStdString();
        EXPECT_FALSE(l.rect.isEmpty()) << d.file << " " << l.text.toStdString();
        EXPECT_FALSE(l.component->toolTip().isEmpty()) << d.file << " " << l.text.toStdString();
    }
    // The four pipeline registers are named in order.
    if (QString(d.processor) != "rvss")
        for (const char *reg : {"IF/ID", "ID/EX", "EX/MEM", "MEM/WB"})
            EXPECT_TRUE(std::any_of(labels.begin(), labels.end(),
                                    [&](const CircuitScene::ComponentLabel &l) { return l.text == reg; }))
                << d.file << " " << reg;
}

TEST_P(CircuitDiagram, LabelsAndLegendDoNotOverlap)
{
    const Design &d      = kDesigns[GetParam()];
    auto          scene  = load(d);
    const auto    labels = scene->componentLabels();
    for (int a = 0; a < labels.size(); ++a)
        for (int b = a + 1; b < labels.size(); ++b)
            EXPECT_FALSE(labels[a].rect.intersects(labels[b].rect))
                << d.file << ": \"" << labels[a].text.toStdString() << "\" and \"" << labels[b].text.toStdString()
                << "\"";
    // A caption (a name outside its component) covers no component.
    QList<QRectF> components;
    for (QGraphicsItem *item : scene->items())
        if (auto *c = dynamic_cast<BaseComponent *>(item))
            components << c->mapRectToScene(c->boundingRect());
    for (const auto &l : labels)
        if (l.caption)
            for (const QRectF &r : components)
                EXPECT_FALSE(l.rect.intersects(r)) << d.file << ": caption \"" << l.text.toStdString() << "\"";
    QGraphicsItem *legend = scene->legendItem();
    ASSERT_NE(legend, nullptr) << d.file;
    const QRectF lr = legend->sceneBoundingRect();
    for (const QRectF &r : components)
        EXPECT_FALSE(lr.intersects(r)) << d.file << ": the legend covers a component";
    for (const auto &l : labels)
        EXPECT_FALSE(lr.intersects(l.rect)) << d.file << ": the legend covers \"" << l.text.toStdString() << "\"";
    EXPECT_GE(scene->legendEntries(), QString(d.processor) == "rvss" ? 2 : 3) << d.file;
}

TEST_P(CircuitDiagram, PipelinesHaveStageHeadersInOrder)
{
    const Design &d       = kDesigns[GetParam()];
    auto          scene   = load(d);
    const auto    headers = scene->stageHeaders();
    if (QString(d.processor) == "rvss")
    {
        EXPECT_TRUE(headers.isEmpty()) << "single-cycle: no stages";
        return;
    }
    ASSERT_EQ(headers.size(), 5) << d.file;
    const char *names[] = {"IF", "ID", "EX", "MEM", "WB"};
    for (int s = 0; s < 5; ++s)
    {
        EXPECT_EQ(headers[s].first, names[s]) << d.file;
        if (s > 0)
            EXPECT_LT(headers[s - 1].second.right(), headers[s].second.left()) << d.file << " " << names[s];
    }
    // A header covers no component, name, the legend or the instruction table.
    QList<QRectF> taken;
    for (QGraphicsItem *item : scene->items())
        if (auto *c = dynamic_cast<BaseComponent *>(item))
            taken << c->mapRectToScene(c->boundingRect());
    for (const auto &l : scene->componentLabels())
        taken << l.rect;
    taken << scene->legendItem()->sceneBoundingRect();
    for (QGraphicsItem *item : scene->items())
        if (dynamic_cast<QGraphicsProxyWidget *>(item))
            taken << item->sceneBoundingRect();
    for (const auto &h : headers)
        for (const QRectF &r : taken)
            EXPECT_FALSE(h.second.intersects(r)) << d.file << " header " << h.first.toStdString();
}

INSTANTIATE_TEST_SUITE_P(Processors, CircuitDiagram, ::testing::Values(0, 1, 2, 3, 4),
                         [](const ::testing::TestParamInfo<size_t> &info) {
                             return std::string(kDesigns[info.param].processor);
                         });

// Names outside components and the legend's text sit on the diagram's
// background: 4.5:1 in every theme. Names inside a component sit on its
// fill: 4.5:1 there.
TEST(CircuitDiagramColours, LabelsMeetFourAndAHalfInEveryTheme)
{
    QDir             dir("../resources/themes");
    const QStringList files = dir.entryList({"*.json"}, QDir::Files);
    ASSERT_EQ(files.size(), 10);
    for (const QString &name : files)
    {
        QFile f(dir.filePath(name));
        ASSERT_TRUE(f.open(QIODevice::ReadOnly));
        const QJsonObject   colors = QJsonDocument::fromJson(f.readAll()).object()["colors"].toObject();
        const DiagramColors d      = ThemeManager::diagramColorsFrom(colors);
        const QColor        text   = CircuitScene::captionColorFor(d);
        EXPECT_GE(contrastRatio(text, d.background), 4.5) << name.toStdString() << " captions and legend";
        const QColor inside = BaseComponent::nameColorFor(d);
        EXPECT_GE(contrastRatio(inside, d.component), 4.5) << name.toStdString() << " names inside components";
    }
}
