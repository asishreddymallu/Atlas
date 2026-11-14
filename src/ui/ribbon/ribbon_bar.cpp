#include "ribbon_bar.h"

#include "ribbon_group.h"
#include "ribbon_page.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QResizeEvent>
#include <QToolButton>
#include <QStackedWidget>
#include <QTabBar>
#include <QVBoxLayout>

namespace Kites
{
/**
 * @brief The band below the tab strip, carrying a hairline at each edge.
 *
 * The rules are two 1 px QFrames held OUTSIDE the layout and raised above
 * everything else in the panel. Three simpler things were tried first and
 * each was measured to draw nothing:
 *
 *  - `#ribbonPanel { border-bottom }`, which had been in template.qss since
 *    the panel was built. Qt's stylesheet style fills a plain QWidget's
 *    background through PE_Widget but does not draw its border, so the
 *    background half of that rule worked and the border half never did.
 *  - a line painted in RibbonBar::paintEvent - overpainted, because the bar
 *    is completely covered by the strip and this panel.
 *  - a line painted in this widget's own paintEvent - overpainted in turn by
 *    the persistent area and the page stack, which fill it edge to edge.
 *
 * A raised child cannot be overpainted by a sibling, takes its colour from
 * the same `{{border}}` token as `ribbonGroupDivider`, and costs no height:
 * it is positioned by hand on row 0 and row height()-1, so the panel stays
 * 92 px and the chrome stays 158.
 */
class RibbonPanel : public QWidget
{
  public:
    explicit RibbonPanel(QWidget *parent) : QWidget(parent)
    {
        for (QFrame **rule : {&m_topRule, &m_bottomRule})
        {
            *rule = new QFrame(this);
            (*rule)->setObjectName("ribbonRule");
            (*rule)->setFrameShape(QFrame::NoFrame);
        }
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        m_topRule->setGeometry(0, 0, width(), 1);
        m_bottomRule->setGeometry(0, height() - 1, width(), 1);
        // After every resize, because the page stack and the persistent area
        // are added to the layout after these are made and would otherwise
        // sit above them.
        m_topRule->raise();
        m_bottomRule->raise();
    }

  private:
    QFrame *m_topRule    {nullptr};
    QFrame *m_bottomRule {nullptr};
};

RibbonBar::RibbonBar(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    m_tabBar = new QTabBar(this);
    // Object name scopes this bar's stylesheet rules so they cannot reach the
    // Cache tab's own QTabWidget, which the generic QTabBar rules still style.
    m_tabBar->setObjectName("ribbonTabBar");
    m_tabBar->setIconSize(QSize(tabStripIconSize, tabStripIconSize));
    m_tabBar->setDrawBase(false);
    m_tabBar->setExpanding(false);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setElideMode(Qt::ElideNone);
    // The strip is the chrome's single tab stop; arrow keys move between tabs
    // from there. Ribbon buttons stay out of the tab chain (see RibbonGroup).
    m_tabBar->setFocusPolicy(Qt::TabFocus);

    auto *panel = new RibbonPanel(this);
    panel->setObjectName("ribbonPanel");
    m_panel = panel;
    auto *panelRow = new QHBoxLayout(panel);
    panelRow->setContentsMargins(0, 0, 0, 0);
    panelRow->setSpacing(0);

    m_persistent = new QWidget(panel);
    auto *persistentRow = new QHBoxLayout(m_persistent);
    persistentRow->setContentsMargins(0, 0, 0, 0);
    persistentRow->setSpacing(0);

    m_pages = new QStackedWidget(panel);

    // Right of the pages: the overflow chevron. A style arrow rather than a
    // text glyph, because which glyphs a font has varies and a missing one
    // would draw a box. No new SVG either - this is a control, not an idea
    // the icon set has a picture for.
    m_chevron = new QToolButton(panel);
    m_chevron->setObjectName("ribbonOverflowButton");
    //
    // The arrow type and InstantPopup together make the style draw TWO
    // arrows - the arrow type where the icon would go, and a menu indicator
    // in the button's own corner - so the stylesheet suppresses the
    // indicator (`#ribbonOverflowButton::menu-indicator`). The arrow type is
    // the one that is centred and scaled with the button, so it is the one
    // that stays.
    m_chevron->setArrowType(Qt::DownArrow);
    m_chevron->setPopupMode(QToolButton::InstantPopup);
    m_chevron->setAutoRaise(true);
    m_chevron->setToolTip(QObject::tr("More commands on this tab"));
    m_chevron->setAccessibleName(QObject::tr("More commands"));
    m_chevron->setAccessibleDescription(
        QObject::tr("Commands that do not fit on the ribbon at this window width"));
    // The one ribbon button that IS a tab stop. Everything behind it is
    // reachable no other way from the keyboard, and a menu navigates by
    // keyboard once it is open. Recorded in docs/audit/known_issues.md.
    m_chevron->setFocusPolicy(Qt::TabFocus);
    m_chevron->hide();

    m_overflowMenu = new QMenu(m_chevron);
    m_chevron->setMenu(m_overflowMenu);
    // Built when it opens, from the overflow set as it is at that moment, so
    // a menu can never list a group that is back on the panel.
    connect(m_overflowMenu, &QMenu::aboutToShow, this, &RibbonBar::rebuildOverflowMenu);

    panelRow->addWidget(m_persistent, 0);
    panelRow->addWidget(m_pages, 1);
    panelRow->addWidget(m_chevron, 0, Qt::AlignVCenter);

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
                // The new page has its own groups and its own widths, so what
                // fits has to be decided again before it is seen.
                applyOverflowLayout();
                emit currentChanged(index);
            });
}

QToolButton *RibbonBar::overflowButton() const
{
    return m_chevron;
}

QList<RibbonGroup *> RibbonBar::overflowedGroups() const
{
    return m_overflowed;
}

RibbonLayoutInput RibbonBar::layoutInput() const
{
    RibbonLayoutInput input;
    input.availableWidth = width();
    input.chevronWidth   = m_chevron->sizeHint().width();

    for (RibbonGroup *group : m_persistentGroups)
        input.persistent.append(
            {group->fullWidth(), group->compactWidth(), group->isOverflowable()});

    if (auto *page = qobject_cast<RibbonPage *>(m_pages->currentWidget()))
        for (RibbonGroup *group : page->groups())
            input.pageGroups.append(
                {group->fullWidth(), group->compactWidth(), group->isOverflowable()});
    return input;
}

void RibbonBar::applyOverflowLayout()
{
    // Hiding a group changes this widget's width, which arrives back here as
    // a resize. The result is a pure function of the width, so the second
    // pass computes the same answer and applying it changes nothing - but
    // there is no reason to walk the tree twice.
    if (m_applying)
        return;
    m_applying = true;

    const RibbonLayoutInput  input  = layoutInput();
    const RibbonLayoutResult result = computeRibbonLayout(input);

    auto *page = qobject_cast<RibbonPage *>(m_pages->currentWidget());
    const QList<RibbonGroup *> pageGroups = page ? page->groups() : QList<RibbonGroup *>{};

    for (RibbonGroup *group : m_persistentGroups)
        group->setCompact(result.compact);
    for (RibbonGroup *group : pageGroups)
        group->setCompact(result.compact);

    m_overflowed.clear();
    for (int i = 0; i < pageGroups.size() && i < result.pageGroupInMenu.size(); ++i)
    {
        const bool inMenu = result.pageGroupInMenu.at(i);
        pageGroups.at(i)->setVisible(!inMenu);
        if (inMenu)
            m_overflowed.append(pageGroups.at(i));
    }

    m_chevron->setVisible(result.chevronShown);

    m_applied    = result;
    m_hasApplied = true;
    m_applying   = false;
}

void RibbonBar::rebuildOverflowMenu()
{
    m_overflowMenu->clear();
    for (RibbonGroup *group : m_overflowed)
    {
        QMenu *submenu = m_overflowMenu->addMenu(group->caption());
        for (QToolButton *button : group->buttons())
            if (QAction *action = button->defaultAction())
                // The action itself, never a copy. Enabled, checked, text
                // and any exclusive group come with it, and no shortcut is
                // added because none is set here.
                submenu->addAction(action);
    }
}

QSize RibbonBar::minimumSizeHint() const
{
    const QSize base = QWidget::minimumSizeHint();
    // The pages contribute nothing (RibbonPage::minimumSizeHint is zero wide),
    // so this is the persistent groups compact plus the chevron. If a larger
    // font pushes that past what the content needs, the window floor rises
    // to it rather than anything being clipped.
    return QSize(ribbonMinimumWidth(const_cast<RibbonBar *>(this)->layoutInput()),
                 base.height());
}

void RibbonBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    applyOverflowLayout();
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

RibbonPage *RibbonBar::currentPage() const
{
    return qobject_cast<RibbonPage *>(m_pages->currentWidget());
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
