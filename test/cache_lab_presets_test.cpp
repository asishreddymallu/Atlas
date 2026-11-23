/**
 * @file cache_lab_presets_test.cpp
 * @brief The lab presets (src/cache_lab/lab_presets.*): the table, and that
 *        choosing one in the Cache tab sets L1 and the step delay.
 */
#include "cache_lab/lab_presets.h"
#include "processor/memory_controller.h"
#include "processor/processor_manager.h"
#include "ui/cache_tab/cache_config_widget.h"
#include "ui/cache_tab/cache_tab.h"
#include "ui/main_window/main_window.h"
#include "ui/timing_grid/timing_grid.h"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QSpinBox>

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace Atlas;

TEST(CacheLabPresets, TheTableHasEveryLabSetupInOrder)
{
    std::vector<std::string> names;
    for (const LabPreset &p : labPresets())
        names.emplace_back(p.name);
    EXPECT_EQ(names, (std::vector<std::string>{"dm256", "dm1024", "dm128", "2w128-fifo", "dm256-32B",
                                               "2w256-lru", "2w256-fifo", "4w256-lru",
                                               "2w256-lru-wb", "2w256-fifo-wb", "4w512-wb"}));
    for (const LabPreset &p : labPresets())
        EXPECT_FALSE(p.description.empty()) << p.name;
}

TEST(CacheLabPresets, EveryPresetIsTheSizeItsNameSays)
{
    struct Want
    {
        const char *name;
        size_t      sets, ways, lineBytes, total;
    };
    for (const Want &w : {Want{"dm256", 16, 1, 16, 256}, Want{"dm1024", 64, 1, 16, 1024},
                          Want{"dm128", 8, 1, 16, 128}, Want{"2w128-fifo", 8, 2, 8, 128},
                          Want{"dm256-32B", 8, 1, 32, 256}, Want{"2w256-lru", 8, 2, 16, 256},
                          Want{"2w256-fifo", 8, 2, 16, 256}, Want{"4w256-lru", 4, 4, 16, 256},
                          Want{"2w256-lru-wb", 8, 2, 16, 256}, Want{"2w256-fifo-wb", 8, 2, 16, 256},
                          Want{"4w512-wb", 4, 4, 32, 512}})
    {
        const LabPreset *p = findLabPreset(w.name);
        ASSERT_NE(p, nullptr) << w.name;
        EXPECT_EQ(p->sets(), w.sets) << w.name;
        EXPECT_EQ(p->ways(), w.ways) << w.name;
        EXPECT_EQ(p->lineBytes(), w.lineBytes) << w.name;
        EXPECT_EQ(p->totalBytes(), w.total) << w.name;
        EXPECT_EQ(p->config().lineCount, w.sets) << "CacheConfig::lineCount is the set count";
    }
    EXPECT_EQ(findLabPreset("nope"), nullptr);
}

TEST(CacheLabPresets, ThePoliciesAreTheLabsOwn)
{
    for (const LabPreset &p : labPresets())
    {
        const std::string name(p.name);
        const bool wb = name.ends_with("-wb");
        EXPECT_EQ(p.writeHit, wb ? WritePolicy::WriteBack : WritePolicy::WriteThrough) << name;
        EXPECT_EQ(p.writeMiss, wb ? AllocationPolicy::WriteAllocate : AllocationPolicy::NoWriteAllocate)
            << name;
        EXPECT_EQ(p.replacement, name.find("fifo") != std::string::npos ? ReplacementPolicy::FIFO
                                                                       : ReplacementPolicy::LRU)
            << name;
    }
}

namespace
{
QSpinBox *stepDelaySpin(MainWindow &window)
{
    for (QSpinBox *spin : window.findChildren<QSpinBox *>())
        if (spin->suffix() == QStringLiteral(" ms"))
            return spin;
    return nullptr;
}
} // namespace

TEST(CacheLabPresets, ChoosingOneSetsL1AndTheStepDelayInOneClick)
{
    MainWindow window;
    auto *tab   = window.findChild<CacheTab *>();
    auto *combo = window.findChild<QComboBox *>(QStringLiteral("labSetupComboBox"));
    auto *panel = window.findChild<TimingGridPanel *>(QStringLiteral("timingGridPanel"));
    ASSERT_TRUE(tab && combo && panel);
    QSpinBox *delay = stepDelaySpin(window);
    ASSERT_NE(delay, nullptr);
    EXPECT_EQ(combo->count(), static_cast<int>(labPresets().size()) + 1) << "None plus every preset";
    EXPECT_EQ(combo->currentIndex(), 0);

    for (const LabPreset &preset : labPresets())
    {
        delay->setValue(500);
        combo->setCurrentIndex(combo->findData(QString::fromUtf8(preset.name.data())));
        QApplication::processEvents();
        const CacheConfig have =
            panel->manager()->getMemoryController()->getL1Cache()->getConfig();
        EXPECT_EQ(have.lineCount, preset.sets()) << preset.name;
        EXPECT_EQ(have.wayCount, preset.ways()) << preset.name;
        EXPECT_EQ(have.lineSizeInBytes, preset.lineBytes()) << preset.name;
        EXPECT_EQ(have.writePolicy, preset.writeHit) << preset.name;
        EXPECT_EQ(have.allocationPolicy, preset.writeMiss) << preset.name;
        EXPECT_EQ(have.replacementPolicy, preset.replacement) << preset.name;
        EXPECT_EQ(delay->value(), 1) << preset.name;
        EXPECT_EQ(tab->activeLabPreset().toStdString(), std::string(preset.name));
        EXPECT_EQ(window.findChild<QLabel *>(QStringLiteral("labSetupDescription"))->text().toStdString(),
                  std::string(preset.description));
        // The L1 sub-tab shows what was set, as a user's edit would.
        auto *l1 = window.findChild<CacheConfigWidget *>(QStringLiteral("L1Config"));
        EXPECT_EQ(l1->getLinesExponent(), preset.setsExponent) << preset.name;
        EXPECT_EQ(l1->getWaysExponent(), preset.waysExponent) << preset.name;
        EXPECT_EQ(l1->getWordsExponent(), preset.wordsExponent) << preset.name;
    }
}

TEST(CacheLabPresets, EditingL1ByHandLeavesThePreset)
{
    MainWindow window;
    auto *tab = window.findChild<CacheTab *>();
    ASSERT_NE(tab, nullptr);
    ASSERT_TRUE(tab->applyLabPreset(QStringLiteral("2w256-lru")));
    EXPECT_EQ(tab->activeLabPreset(), QStringLiteral("2w256-lru"));
    auto *ways = window.findChild<CacheConfigWidget *>(QStringLiteral("L1Config"))
                     ->findChild<QSpinBox *>(QStringLiteral("waysSpinBox"));
    ways->setValue(ways->value() + 1);
    QApplication::processEvents();
    EXPECT_TRUE(tab->activeLabPreset().isEmpty());
    EXPECT_FALSE(tab->applyLabPreset(QStringLiteral("no such preset")));
}
