#pragma once
#include "ui/theme/icon_manager.h"

#include <QList>
#include <QString>
#include <QWidget>

class QStackedWidget;
class QTabBar;

namespace Kites
{
class RibbonGroup;
class RibbonPage;

/**
 * @brief The horizontal tab strip and the ribbon panel below it.
 *
 * Layout:
 *
 *     [ Editor | Memory | Processor | Cache | Compiler | Profiler ]  <- QTabBar
 *     [ persistent groups ][ the current tab's RibbonPage         ]  <- panel
 *
 * The persistent area is deliberately outside the page stack. Groups that
 * apply on every tab (execution transport, file commands) therefore exist
 * exactly once rather than once per tab, so a widget that cannot be shared -
 * the step-interval spin box, say - has a single unambiguous home.
 *
 * The bar is self-contained: it holds no pointer to the window and knows
 * nothing about what the tabs contain. MainWindow keeps it in step with the
 * content stack through its own setCurrentTab().
 */
class RibbonBar : public QWidget
{
    Q_OBJECT

  public:
    explicit RibbonBar(QWidget *parent = nullptr);

    /// Appends a tab and its (initially empty) page. Returns the new index.
    int addTab(const QString &text, Icon icon);

    int         count() const;
    RibbonPage *page(int index) const;

    /// A group shown to the left of the page area on every tab.
    RibbonGroup *addPersistentGroup(const QString &caption);
    int          persistentGroupCount() const;
    RibbonGroup *persistentGroup(int index) const;

    int  currentIndex() const;
    void setCurrentIndex(int index);

    /// Exposed so the window can set focus policy and tab order on it; the
    /// bar does not expect anyone else to drive it.
    QTabBar *tabBar() const;

    /**
     * @brief Re-reads IconManager for every tab icon.
     *
     * Tab icons are QIcons on a QTabBar, not QActions, so the window-wide
     * re-tint pass over findChildren<QAction*>() does not reach them. Ribbon
     * buttons need no equivalent: each takes its icon from its default
     * action, which that pass does cover.
     */
    void retintIcons();

  signals:
    /// The visible tab changed, whether by click, by keyboard or by
    /// setCurrentIndex().
    void currentChanged(int index);

  private:
    QTabBar        *m_tabBar     {nullptr};
    QStackedWidget *m_pages      {nullptr};
    QWidget        *m_persistent {nullptr};

    QList<Icon>          m_tabIcons;
    QList<RibbonGroup *> m_persistentGroups;
};
} // namespace Kites
