#pragma once
#include <QList>
#include <QString>
#include <QWidget>

class QHBoxLayout;

namespace Kites
{
class RibbonGroup;

/**
 * @brief The per-tab half of the ribbon panel: a row of RibbonGroups.
 *
 * One page exists per tab and only the current tab's page is visible. The
 * groups that apply to every tab do not live here - RibbonBar keeps those in
 * a persistent area beside the pages, so there is exactly one instance of
 * each of those buttons rather than one per tab.
 */
class RibbonPage : public QWidget
{
    Q_OBJECT

  public:
    explicit RibbonPage(QWidget *parent = nullptr);

    /// Creates a group, appends it to the right of the existing ones and
    /// returns it. The page owns it.
    RibbonGroup *addGroup(const QString &caption);

    int                  groupCount() const;
    RibbonGroup         *group(int index) const;
    QList<RibbonGroup *> groups() const;

    /// Puts every group into (or out of) icon-only mode. Stage 5 drives this
    /// from the available width.
    void setCompact(bool compact);
    bool isCompact() const;

    /**
     * @brief Zero horizontal minimum, deliberately.
     *
     * The pages live in a QStackedWidget, which takes the largest of its
     * children's minimums - so without this the busiest page would set the
     * floor for every tab, and the window could never be narrower than the
     * Processor page needs. Every group on a page can go into the overflow
     * menu instead, so a page genuinely needs no width of its own. The
     * height is left alone.
     */
    QSize minimumSizeHint() const override;

  private:
    QHBoxLayout         *m_row {nullptr};
    QList<RibbonGroup *> m_groups;
    bool                 m_compact {false};
};
} // namespace Kites
