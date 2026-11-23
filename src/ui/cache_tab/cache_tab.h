#pragma once
#include "cache_grid_delegate.h"
#include "cache_config_widget.h"
#include "cache_model.h"
#include "cache_live_observer.h"
#include "ui/common/atlas_tab.h"
#include "processor/memory_controller.h"
#include <QWidget>

class QTimer;
class QComboBox;
class QLabel;
class QPushButton;


namespace Atlas
{
class CacheLiveView;

namespace Ui
{
class CacheTab;
}

class CacheTab : public AtlasTab
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

    /// The live cache-line view of a level, and the observer behind it.
    CacheLiveView *liveView(CacheLevel level) const;
    CacheLiveObserver   *liveObserver(CacheLevel level) const;

public slots:
    /// Takes a full copy of every level's lines and shows it now. Call only
    /// when no run is changing the caches (GUI-thread step, undo, load,
    /// reconfigure, a finished run); during a run the observers keep the
    /// views current from the accesses themselves.
    void resyncLiveView();
    /// Shows whatever the observers have, if it changed. The 50 ms timer
    /// calls it; tests may too.
    void refreshLiveView();
    /// Sets L1 to the lab preset called `name` through the L1 sub-tab's own
    /// fields, as a user would, and announces it (the window then sets the
    /// step delay). False, changing nothing, for an unknown name.
    bool applyLabPreset(const QString &name);

public:
    /// The preset L1 currently matches, or empty when it was set by hand.
    QString activeLabPreset() const;

signals:
    /// Emitted whenever the inner tab widget lands on another level,
    /// whoever moved it, so a second view of the same choice can follow it.
    void cacheLevelChangedSignal(CacheLevel level);

private:
	//void updateCacheConfig(CacheLevel cacheLevel, CacheConfig newConfig);
    bool enforceL2AtLeastL1();
    /// Applies what the three sub-tabs show to the current caches.
    void applyShownConfigs();
    bool m_widgetsShowEngine = false; ///< set once the widgets were filled from the engine

    std::array<CacheModel*, CacheLevelCount> m_cacheModels{};
    std::array<CacheLiveObserver*, CacheLevelCount> m_liveObservers{};
    std::array<CacheLiveView*, CacheLevelCount> m_liveViews{};
    std::array<uint64_t, CacheLevelCount> m_shownVersions{};
    QTimer *m_liveTimer{nullptr};
    void attachLiveObservers();
    MemoryController  *m_memoryController    {nullptr};
    Ui::CacheTab *ui;
    QComboBox *m_labSetupCombo{nullptr};
    QLabel    *m_labSetupDescription{nullptr};
    /// What the L1 sub-tab's configChangedSignal does: reconfigure (L2 first
    /// if it must grow), resync the live view, and drop a preset L1 no
    /// longer matches.
    void applyL1Config();
signals:
    void cacheConfigChangedSignal(CacheLevel cacheLevel, CacheConfig newConfig);
    /// A lab preset was chosen; carries its name.
    void labPresetChosenSignal(const QString &name);
};
} // namespace Atlas
