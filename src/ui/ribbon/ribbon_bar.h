#pragma once
#include "ui/ribbon/ribbon_layout.h"
#include "ui/theme/icon_manager.h"

#include <QList>
#include <QString>
#include <QWidget>

class QMenu;
class QResizeEvent;
class QStackedWidget;
class QToolButton;
class QTabBar;

namespace Kites
{
class RibbonGroup;
class RibbonPage;
class RibbonPanel;

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
    /// The page the panel is showing. Always page(currentIndex()); it exists
    /// so that invariant can be asserted from outside.
    RibbonPage *currentPage() const;

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

    /**
     * @brief The overflow chevron, shown only when something is behind it.
     *
     * A tab stop, unlike every other button on the ribbon. That is a
     * deliberate exception to the one-chrome-stop rule: it is the only
     * keyboard route to a command whose group has moved into the menu, and
     * a QMenu is navigable by keyboard once it is open.
     */
    QToolButton *overflowButton() const;

    /// The current page's groups that are in the menu rather than on the
    /// panel, in page order. Held explicitly, never read back from
    /// isVisible() - which is false for every widget in a window that has
    /// not been shown.
    QList<RibbonGroup *> overflowedGroups() const;

    /// Recomputes what fits at the current width and applies it. Called on
    /// resize and on a page change; safe to call at any time.
    void applyOverflowLayout();

    QSize minimumSizeHint() const override;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  signals:
    /// The visible tab changed, whether by click, by keyboard or by
    /// setCurrentIndex().
    void currentChanged(int index);

  private:
    /// Fills the chevron's menu from the CURRENT overflow set, so it can
    /// never show a group that has since come back onto the panel.
    void rebuildOverflowMenu();
    /// Builds the layout function's input from the live groups.
    RibbonLayoutInput layoutInput() const;

    QTabBar        *m_tabBar     {nullptr};
    // The band below the strip. It draws the two hairlines itself, on its
    // own first and last pixel rows.
    RibbonPanel    *m_panel      {nullptr};
    QStackedWidget *m_pages      {nullptr};
    QWidget        *m_persistent {nullptr};
    QToolButton    *m_chevron    {nullptr};
    QMenu          *m_overflowMenu {nullptr};

    // The current page's hidden groups, held explicitly rather than read
    // back from isVisible() - which is false for every widget in a window
    // that has not been shown, and would make the tests lie.
    QList<RibbonGroup *> m_overflowed;
    RibbonLayoutResult   m_applied;
    bool                 m_hasApplied {false};
    // Guards against a resize triggered by our own show/hide re-entering.
    bool m_applying {false};

    QList<Icon>          m_tabIcons;
    QList<RibbonGroup *> m_persistentGroups;
};
} // namespace Kites
