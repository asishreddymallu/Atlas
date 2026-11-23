#include "cache_tab.h"
#include "ui/common/data_table_style.h"
#include "cache_grid_delegate.h"
#include "cache_config_widget.h"
#include "ui_cache_tab.h"
#include "cache_live_view.h"
#include <QSplitter>
#include <QTableView>
#include <QTimer>
#include <QLayout>
#include <QMargins>
#include <QSignalBlocker>
#include <QTabWidget>
#include <algorithm>
#include <tuple>
#include <iterator>

namespace Atlas
{
namespace
{
// The inner tab widget's page order, which is deliberately not the order of
// the CacheLevel enum: the enum indexes m_cacheModels (L1, L2, Instruction)
// while the .ui puts the instruction cache first. One table, read both ways,
// so the two orders cannot be confused for each other.
constexpr CacheTab::CacheLevel tabOrder[] = {
    CacheTab::Instruction,
    CacheTab::L1,
    CacheTab::L2,
};

int tabIndexFor(CacheTab::CacheLevel level)
{
    for (int index = 0; index < static_cast<int>(std::size(tabOrder)); ++index)
        if (tabOrder[index] == level)
            return index;
    return -1;
}

CacheTab::CacheLevel levelForTabIndex(int index)
{
    if (index < 0 || index >= static_cast<int>(std::size(tabOrder)))
        return CacheTab::CacheLevelCount;
    return tabOrder[index];
}
} // namespace

CacheTab::CacheTab(QWidget *parent, MemoryController *memoryController)
   : AtlasTab(parent), m_memoryController(memoryController),ui(new Ui::CacheTab)
{
    assert(m_memoryController && "MemoryController must not be null when initializing CacheTab");
    ui->setupUi(this);
    m_cacheModels[CacheLevel::Instruction] = new CacheModel(
        this, m_memoryController ? m_memoryController->getInstructionCache() : nullptr);
    m_cacheModels[CacheLevel::L1] =
        new CacheModel(this, m_memoryController ? m_memoryController->getL1Cache() : nullptr);
    m_cacheModels[CacheLevel::L2] =
        new CacheModel(this, m_memoryController ? m_memoryController->getL2Cache() : nullptr);

    ui->splitter_2->setStretchFactor(1, 1);
    ui->splitter_3->setStretchFactor(1, 1);
    ui->splitter_4->setStretchFactor(1, 1);

    ui->L1tableView->setModel(m_cacheModels[CacheLevel::L1]);
    ui->L2tableView->setModel(m_cacheModels[CacheLevel::L2]);
    ui->InstructiontableView->setModel(m_cacheModels[CacheLevel::Instruction]);

    applyDataTableStyle(ui->L1tableView);
    ui->L1tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    applyDataTableStyle(ui->L2tableView);
    ui->L2tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    applyDataTableStyle(ui->InstructiontableView);
    ui->InstructiontableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    ui->L1tableView->setItemDelegate(new CacheGridDelegate(this));
    ui->L2tableView->setItemDelegate(new CacheGridDelegate(this));
    ui->InstructiontableView->setItemDelegate(new CacheGridDelegate(this));

    // The live cache-line view: in each sub-tab the right side of the
    // splitter becomes a vertical splitter, the live view above the existing
    // per-line data table. Built here so the .ui and every widget name in it
    // stay as they are.
    const std::tuple<CacheLevel, QSplitter *, QTableView *> livePages[] = {
        {CacheLevel::Instruction, ui->splitter_4, ui->InstructiontableView},
        {CacheLevel::L1, ui->splitter_2, ui->L1tableView},
        {CacheLevel::L2, ui->splitter_3, ui->L2tableView}};
    for (const auto &[level, splitter, table] : livePages)
    {
        m_liveObservers[level] = new CacheLiveObserver(this);
        m_liveViews[level]     = new CacheLiveView(this);
        const int at           = splitter->indexOf(table);
        auto *right            = new QSplitter(Qt::Vertical);
        right->setObjectName(QStringLiteral("cacheLiveSplitter"));
        right->addWidget(m_liveViews[level]);
        right->addWidget(table); // reparents it out of the outer splitter
        right->setStretchFactor(0, 3);
        right->setStretchFactor(1, 2);
        splitter->insertWidget(at, right);
    }
    m_liveTimer = new QTimer(this);
    m_liveTimer->setInterval(50); // at most one refresh every 50 ms during a run
    connect(m_liveTimer, &QTimer::timeout, this, &CacheTab::refreshLiveView);
    m_liveTimer->start();

    ui->tabWidget->setTabText(0, "Instruction Cache");
    ui->tabWidget->setTabText(1, "L1 Cache");
    ui->tabWidget->setTabText(2, "L2 Cache");

    // The inner tab bar started one layout margin right of the ribbon's
    // strip, which made two bars of the same kind look like two different
    // kinds of control. The .ui declares no margins, so Qt supplies its
    // default on all four sides and the whole tab widget is inset by it.
    //
    // Zeroing the left margin pulls the bar flush - but it pulls the pane
    // with it, and the pane's content would then sit a margin left of every
    // other tab's. So the same amount is given back INSIDE each page, which
    // moves the content without moving the bar.
    //
    // The amount is asked of the style rather than written down, so it is
    // the same number the other five tabs' layouts were given. The .ui is
    // untouched.
    if (QLayout *outer = layout())
    {
        const QMargins margins = outer->contentsMargins();
        const int      inset   = margins.left();
        outer->setContentsMargins(0, margins.top(), margins.right(), margins.bottom());

        for (QWidget *page : {ui->tab, ui->L1Tab, ui->L2Tab})
            if (QLayout *pageLayout = page ? page->layout() : nullptr)
            {
                const QMargins pageMargins = pageLayout->contentsMargins();
                pageLayout->setContentsMargins(pageMargins.left() + inset, pageMargins.top(),
                                               pageMargins.right(), pageMargins.bottom());
            }
    }

    ui->tabWidget->setCurrentIndex(0); // show Instruction cache by default

    // Connected after the default is set, so construction does not announce
    // a change nobody asked for.
    connect(ui->tabWidget, &QTabWidget::currentChanged, this,
            [this](int index) { emit cacheLevelChangedSignal(levelForTabIndex(index)); });
    // ui->splitter->setStretchFactor(0, 0);
    // ui->splitter->setStretchFactor(1, 1);

    changeMemoryController(m_memoryController);
    
    connect(ui->L1Config, &CacheConfigWidget::configChangedSignal, this, [this]{
        if (enforceL2AtLeastL1())
        {
            m_memoryController->getL2Cache()->reconfigure(ui->L2Config->getConfig());
        }
        m_memoryController->getL1Cache()->reconfigure(ui->L1Config->getConfig());
        resyncLiveView();
    });
    connect(ui->InstructionConfig, &CacheConfigWidget::configChangedSignal, this, [this]{
        if(enforceL2AtLeastL1())
        {
            m_memoryController->getL2Cache()->reconfigure(ui->L2Config->getConfig());
        }
        m_memoryController->getInstructionCache()->reconfigure(ui->InstructionConfig->getConfig());
        resyncLiveView();
    });
    connect(ui->L2Config, &CacheConfigWidget::configChangedSignal, this, [this]{
        m_memoryController->getL2Cache()->reconfigure(ui->L2Config->getConfig());
        resyncLiveView();
    });

    //set initial config values in the UI from the memory controller's caches
    ui->L1Config->setConfig(m_memoryController->getL1Cache()->getConfig());
    ui->L2Config->setConfig(m_memoryController->getL2Cache()->getConfig());
    ui->InstructionConfig->setConfig(m_memoryController->getInstructionCache()->getConfig());
    m_widgetsShowEngine = true;
    resyncLiveView();

}

// void CacheTab::updateCacheConfig(CacheLevel cacheLevel, CacheConfig newConfig)
// {
//     switch(cacheLevel)
//     {
//         case CacheLevel::L1:
//             m_memoryController->getL1Cache()->reconfigure(newConfig);
//             break;
//         case CacheLevel::L2:
//             m_memoryController->getL2Cache()->reconfigure(newConfig);
//             break;
//         case CacheLevel::Instruction:
//             m_memoryController->getInstructionCache()->reconfigure(newConfig);
//             break;
//     }
// }

void CacheTab::setCacheLevel(CacheLevel level)
{
    const int index = tabIndexFor(level);
    if (index >= 0)
        ui->tabWidget->setCurrentIndex(index);
}

CacheTab::CacheLevel CacheTab::currentCacheLevel() const
{
    return levelForTabIndex(ui->tabWidget->currentIndex());
}

void CacheTab::changeMemoryController(MemoryController *memoryController)
{
    m_memoryController = memoryController;
    assert(m_memoryController && "MemoryController must not be null when initializing CacheTab");

    // Rebind models to new cache instances.
    m_cacheModels[CacheLevel::Instruction]->attachCache(m_memoryController->getInstructionCache());
    m_cacheModels[CacheLevel::L1]->attachCache(m_memoryController->getL1Cache());
    m_cacheModels[CacheLevel::L2]->attachCache(m_memoryController->getL2Cache());

    // Refresh cache signal wiring from new cache objects.
    // Note: do not disconnect config widgets globally; that would remove
    // their configChanged->cacheConfigChanged wiring installed in constructor.

    connect(ui->L1Config, &CacheConfigWidget::customPolicyScriptSelectedSignal,
            m_memoryController->getL1Cache(), &Cache::loadCustomPolicyScript);
    connect(ui->L2Config, &CacheConfigWidget::customPolicyScriptSelectedSignal,
            m_memoryController->getL2Cache(), &Cache::loadCustomPolicyScript);
    connect(ui->InstructionConfig, &CacheConfigWidget::customPolicyScriptSelectedSignal,
            m_memoryController->getInstructionCache(), &Cache::loadCustomPolicyScript);

    connect(m_memoryController->getL1Cache(), &Cache::cacheStatsUpdatedSignal, ui->L1Config,
            &CacheConfigWidget::cacheStatsUpdatedSlot);
    connect(m_memoryController->getL2Cache(), &Cache::cacheStatsUpdatedSignal, ui->L2Config,
            &CacheConfigWidget::cacheStatsUpdatedSlot);
    connect(m_memoryController->getInstructionCache(), &Cache::cacheStatsUpdatedSignal,
            ui->InstructionConfig, &CacheConfigWidget::cacheStatsUpdatedSlot);

    connect(m_memoryController->getL1Cache(), &Cache::customPolicyScriptLoadedSignal, ui->L1Config,
            &CacheConfigWidget::customPolicyScriptLoadedSlot);
    connect(m_memoryController->getL2Cache(), &Cache::customPolicyScriptLoadedSignal, ui->L2Config,
            &CacheConfigWidget::customPolicyScriptLoadedSlot);
    connect(m_memoryController->getInstructionCache(), &Cache::customPolicyScriptLoadedSignal,
            ui->InstructionConfig, &CacheConfigWidget::customPolicyScriptLoadedSlot);

    // A processor switch builds new caches with the engine's defaults. Keep
    // what the three sub-tabs show: apply it to the new caches. (At
    // construction the widgets are filled FROM the engine instead.)
    if (m_widgetsShowEngine)
        applyShownConfigs();
    attachLiveObservers();
}

void CacheTab::attachLiveObservers()
{
    if (!m_liveObservers[CacheLevel::L1])
        return; // constructor: the views are not built yet
    m_liveObservers[CacheLevel::Instruction]->attach(m_memoryController->getInstructionCache());
    m_liveObservers[CacheLevel::L1]->attach(m_memoryController->getL1Cache());
    m_liveObservers[CacheLevel::L2]->attach(m_memoryController->getL2Cache());
    resyncLiveView();
}

void CacheTab::resyncLiveView()
{
    for (CacheLiveObserver *observer : m_liveObservers)
        if (observer)
            observer->resync();
    refreshLiveView();
}

void CacheTab::refreshLiveView()
{
    for (size_t level = 0; level < CacheLevelCount; ++level)
    {
        CacheLiveObserver *observer = m_liveObservers[level];
        if (!observer || !m_liveViews[level] || observer->version() == m_shownVersions[level])
            continue;
        const CacheLiveObserver::State state = observer->state();
        m_liveViews[level]->showState(state);
        m_shownVersions[level] = state.version;
    }
}

CacheLiveView *CacheTab::liveView(CacheLevel level) const
{
    return level < CacheLevelCount ? m_liveViews[level] : nullptr;
}

CacheLiveObserver *CacheTab::liveObserver(CacheLevel level) const
{
    return level < CacheLevelCount ? m_liveObservers[level] : nullptr;
}

void CacheTab::applyShownConfigs()
{
    const std::pair<CacheConfigWidget *, Cache *> levels[] = {
        {ui->InstructionConfig, m_memoryController->getInstructionCache()},
        {ui->L1Config, m_memoryController->getL1Cache()},
        {ui->L2Config, m_memoryController->getL2Cache()}};
    for (const auto &[widget, cache] : levels)
    {
        CacheConfig config = widget->getConfig();
        // A custom policy needs its script loaded into the new cache: apply
        // the rest with LRU, then load the script (which selects Custom).
        const bool custom = config.replacementPolicy == ReplacementPolicy::Custom;
        if (custom)
            config.replacementPolicy = ReplacementPolicy::LRU;
        cache->reconfigure(config);
        const std::string script = widget->customPolicyScriptPath();
        if (custom && !script.empty())
            cache->loadCustomPolicyScript(script);
        else if (custom)
            widget->setConfig(cache->getConfig()); // nothing to load: show LRU, as the engine has
    }
}

bool CacheTab::enforceL2AtLeastL1()
{
    const int l1Lines = ui->L1Config->getLinesExponent();
    const int l1Ways  = ui->L1Config->getWaysExponent();
    const int l1Words = ui->L1Config->getWordsExponent();

    const int instructionLines = ui->InstructionConfig->getLinesExponent();
    const int instructionWays  = ui->InstructionConfig->getWaysExponent();
    const int instructionWords = ui->InstructionConfig->getWordsExponent();

    const int l2Lines = ui->L2Config->getLinesExponent();
    const int l2Ways  = ui->L2Config->getWaysExponent();
    const int l2Words = ui->L2Config->getWordsExponent();

    const int newL2Lines = std::max({l2Lines, l1Lines, instructionLines});
    const int newL2Ways  = std::max({l2Ways, l1Ways, instructionWays});
    const int newL2Words = std::max({l2Words, l1Words, instructionWords});

    const bool changed =
        (newL2Lines != l2Lines) || (newL2Ways != l2Ways) || (newL2Words != l2Words);
    if (!changed)
    {
        return false;
    }

    {
        QSignalBlocker blocker1(ui->L2Config);
        ui->L2Config->setLinesExponent(newL2Lines);
        ui->L2Config->setWaysExponent(newL2Ways);
        ui->L2Config->setWordsExponent(newL2Words);
    }
    return true;
}

CacheTab::~CacheTab()
{
    delete ui;
}
} // namespace Atlas
