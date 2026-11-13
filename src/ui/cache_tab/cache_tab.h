#pragma once
#include "cache_grid_delegate.h"
#include "cache_config_widget.h"
#include "cache_model.h"
#include "ui/common/kites_tab.h"
#include "processor/memory_controller.h"
#include <QWidget>


namespace Kites
{
namespace Ui
{
class CacheTab;
}

class CacheTab : public KitesTab
{
    Q_OBJECT

public:
    // Indexes m_cacheModels. Note this is NOT the order the inner tab widget
    // shows - the .ui puts the instruction cache first - so the two are
    // mapped explicitly in the .cpp rather than cast into each other.
    // Public since Stage 3 so the ribbon can name a level; the values and
    // their order are unchanged.
    enum CacheLevel
    {
        L1 = 0,
        L2,
        Instruction,
        CacheLevelCount
    };

    explicit CacheTab(QWidget *parent = nullptr, MemoryController *memoryController = nullptr);
    ~CacheTab();
    void changeMemoryController(MemoryController *memoryController);

    /// Shows a cache level by moving the tab's own inner tab widget, which
    /// stays the state. A level the widget has no page for is ignored.
    void setCacheLevel(CacheLevel level);

    /// The level the inner tab widget is currently showing.
    CacheLevel currentCacheLevel() const;

signals:
    /// Emitted whenever the inner tab widget lands on another level,
    /// whoever moved it, so a second view of the same choice can follow it.
    void cacheLevelChangedSignal(CacheLevel level);

private:
	//void updateCacheConfig(CacheLevel cacheLevel, CacheConfig newConfig);
    bool enforceL2AtLeastL1();

    std::array<CacheModel*, CacheLevelCount> m_cacheModels{};
    MemoryController  *m_memoryController    {nullptr};
    Ui::CacheTab *ui;
signals:
    void cacheConfigChangedSignal(CacheLevel cacheLevel, CacheConfig newConfig);
};
} // namespace Kites
