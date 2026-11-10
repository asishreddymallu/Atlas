#include "ribbon_bar.h"

#include "ribbon_group.h"
#include "ribbon_page.h"

#include <QHBoxLayout>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace Kites
{
namespace
{
// Tab-strip icon size in logical pixels, the same value the toolbar uses and
// the one IconManager rasterises by default.
constexpr int stripIconSize = 20;
} // namespace

RibbonBar::RibbonBar(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_tabBar = new QTabBar(this);
    // Object name scopes this bar's stylesheet rules so they cannot reach the
    // Cache tab's own QTabWidget, which the generic QTabBar rules still style.
    m_tabBar->setObjectName("ribbonTabBar");
    m_tabBar->setIconSize(QSize(stripIconSize, stripIconSize));
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideNone);
    // The strip is the chrome's single tab stop; arrow keys move between tabs
    // from there. Ribbon buttons stay out of the tab chain (see RibbonGroup).
    m_tabBar->setFocusPolicy(Qt::TabFocus);

    auto *panel = new QWidget(this);
    panel->setObjectName("ribbonPanel");
    auto *panelRow = new QHBoxLayout(panel);
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->setSpacing(0);

    m_persistent = new QWidget(panel);
    auto *persistentRow = new QHBoxLayout(m_persistent);
    persistentRow->setContentsMargins(0, 0, 0, 0);
    persistentRow->setSpacing(0);

    m_pages = new QStackedWidget(panel);

    panelRow->addWidget(m_persistent, 0);
    panelRow->addWidget(m_pages, 1);

    column->addWidget(m_tabBar, 0);
    column->addWidget(panel, 1);

    // The page stack follows the strip. This is internal bookkeeping: it runs
    // even when the bar's own currentChanged() is blocked by a caller that is
    // already driving both sides, so the panel can never show a page that
    // does not belong to the selected tab.
    connect(m_tabBar, &QTabBar::currentChanged, this,
            [this](int index)
            {
                m_pages->setCurrentIndex(index);
                emit currentChanged(index);
            });
}

int RibbonBar::addTab(const QString &text, Icon icon)
{
    // The page goes in first, so the index the tab bar is about to make
    // current already addresses a real page.
    auto *page = new RibbonPage(m_pages);
    m_pages->addWidget(page);
    m_tabIcons.append(icon);

    return m_tabBar->addTab(IconManager::getInstance().getIcon(icon), text);
}

int RibbonBar::count() const
{
    return m_tabBar->count();
}

RibbonPage *RibbonBar::page(int index) const
{
    if (index < 0 || index >= m_pages->count())
        return nullptr;
    return qobject_cast<RibbonPage *>(m_pages->widget(index));
}

RibbonGroup *RibbonBar::addPersistentGroup(const QString &caption)
{
    auto *group = new RibbonGroup(caption, m_persistent);
    m_persistent->layout()->addWidget(group);
    m_persistentGroups.append(group);
    return group;
}

int RibbonBar::persistentGroupCount() const
{
    return static_cast<int>(m_persistentGroups.size());
}

RibbonGroup *RibbonBar::persistentGroup(int index) const
{
    if (index < 0 || index >= persistentGroupCount())
        return nullptr;
    return m_persistentGroups.at(index);
}

int RibbonBar::currentIndex() const
{
    return m_tabBar->currentIndex();
}

void RibbonBar::setCurrentIndex(int index)
{
    // QTabBar ignores a set-to-the-current-index, so this emits nothing when
    // there is nothing to change and cannot start a signal loop.
    m_tabBar->setCurrentIndex(index);
}

QTabBar *RibbonBar::tabBar() const
{
    return m_tabBar;
}

void RibbonBar::retintIcons()
{
    IconManager &iconManager = IconManager::getInstance();
    for (int index = 0; index < m_tabIcons.size() && index < m_tabBar->count(); ++index)
        m_tabBar->setTabIcon(index, iconManager.getIcon(m_tabIcons.at(index)));
}
} // namespace Kites
