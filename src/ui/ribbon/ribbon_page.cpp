#include "ribbon_page.h"

#include "ribbon_group.h"

#include <QHBoxLayout>

namespace Kites
{
RibbonPage::RibbonPage(QWidget *parent) : QWidget(parent)
{
    m_row = new QHBoxLayout(this);
    m_row->setContentsMargins(0, 0, 0, 0);
    m_row->setSpacing(0);
    // Trailing stretch: groups stay packed to the left however few there are,
    // so a page with one group does not spread it across the window.
    m_row->addStretch(1);
}

RibbonGroup *RibbonPage::addGroup(const QString &caption)
{
    auto *group = new RibbonGroup(caption, this);
    group->setCompact(m_compact);
    m_row->insertWidget(m_row->count() - 1, group);
    m_groups.append(group);
    return group;
}

int RibbonPage::groupCount() const
{
    return static_cast<int>(m_groups.size());
}

RibbonGroup *RibbonPage::group(int index) const
{
    if (index < 0 || index >= groupCount())
        return nullptr;
    return m_groups.at(index);
}

QList<RibbonGroup *> RibbonPage::groups() const
{
    return m_groups;
}

void RibbonPage::setCompact(bool compact)
{
    m_compact = compact;
    for (RibbonGroup *group : std::as_const(m_groups))
        group->setCompact(compact);
}

bool RibbonPage::isCompact() const
{
    return m_compact;
}
} // namespace Kites
