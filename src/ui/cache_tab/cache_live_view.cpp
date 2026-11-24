#include "ui/cache_tab/cache_live_view.h"

#include "ui/cache_tab/cache_set_model.h"
#include "ui/common/data_table_style.h"

#include <QHeaderView>
#include <QLabel>
#include <QListView>
#include <QStringListModel>
#include <QTableView>
#include <QVBoxLayout>

namespace Atlas
{
namespace
{
QLabel *paneTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setProperty("paneTitle", true); // the theme's small muted caption
    return label;
}
} // namespace

CacheLiveView::CacheLiveView(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("cacheLiveView"));
    m_model = new CacheSetModel(this);

    m_table = new QTableView(this);
    m_table->setObjectName(QStringLiteral("cacheSetTable"));
    m_table->setModel(m_model);
    applyDataTableStyle(m_table);
    // Fixed row heights and no resize-to-contents: the view then lays out
    // and asks data() for the visible rows only, whatever the set count.
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);

    m_lastAccess = new QLabel(CacheLiveObserver::describe({}), this);
    m_lastAccess->setObjectName(QStringLiteral("cacheLastAccess"));
    m_lastAccess->setTextInteractionFlags(Qt::TextSelectableByMouse);

    // Metric 6: the misses so far, by cause, beside the last access. It does
    // not set the pane's minimum width (Ignored), so the window keeps its size.
    m_missCauses = new QLabel(this);
    m_missCauses->setObjectName(QStringLiteral("cacheMissCauses"));
    m_missCauses->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_missCauses->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_missCauses->setToolTip(QStringLiteral(
        "[Modelled] Each miss since the cache's Reset, classified:\n"
        "compulsory - the first access to that block;\n"
        "capacity - a fully associative LRU cache with the same number of lines would also have "
        "missed;\n"
        "conflict - every other miss.\n"
        "They add up to the Misses counter. Undo does not restore the cache's counters "
        "(known_issues 83), and these numbers follow the same limitation."));

    m_logModel = new QStringListModel(this);
    m_log      = new QListView(this);
    m_log->setObjectName(QStringLiteral("cacheAccessLog"));
    m_log->setModel(m_logModel);
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_log->setSelectionMode(QAbstractItemView::NoSelection);
    m_log->setUniformItemSizes(true);
    m_log->setMaximumHeight(m_log->sizeHintForRow(0) > 0 ? m_log->sizeHintForRow(0) * 8 : 160);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(paneTitle(QStringLiteral("Lines by set and way"), this));
    layout->addWidget(m_table, 1);
    layout->addWidget(paneTitle(QStringLiteral("Last access"), this));
    layout->addWidget(m_lastAccess);
    layout->addWidget(m_missCauses);
    layout->addWidget(paneTitle(QStringLiteral("Last %1 accesses").arg(CacheLiveObserver::kLogSize),
                                this));
    layout->addWidget(m_log);
}

void CacheLiveView::showState(const CacheLiveObserver::State &state)
{
    const CacheAccessRecord last = state.log.empty() ? CacheAccessRecord{} : state.log.back();
    m_model->setState(state.snapshot, last);
    m_lastAccess->setText(CacheLiveObserver::describe(last));
    m_missCauses->setText(QStringLiteral("Misses by cause [Modelled]: compulsory %1 \u00b7 capacity %2 \u00b7 conflict %3")
                              .arg(state.causes.compulsory)
                              .arg(state.causes.capacity)
                              .arg(state.causes.conflict));

    QStringList lines;
    for (const CacheAccessRecord &record : state.log)
        lines << CacheLiveObserver::describe(record);
    m_logModel->setStringList(lines);
    m_log->scrollToBottom();

    const QModelIndex touched = m_model->touchedIndex();
    if (touched.isValid())
        m_table->scrollTo(touched, QAbstractItemView::EnsureVisible);
}

QString CacheLiveView::lastAccessText() const
{
    return m_lastAccess->text();
}

QString CacheLiveView::missCausesText() const
{
    return m_missCauses->text();
}

QStringList CacheLiveView::logLines() const
{
    return m_logModel->stringList();
}
} // namespace Atlas
