#include "ui/cache_tab/cache_set_model.h"

#include "ui/theme/theme_manager.h"

namespace Atlas
{
CacheSetModel::CacheSetModel(QObject *parent) : QAbstractTableModel(parent) {}

int CacheSetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_snapshot.sets);
}

int CacheSetModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_snapshot.ways);
}

QVariant CacheSetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount() || index.column() >= columnCount())
        return {};
    const size_t set  = static_cast<size_t>(index.row());
    const size_t way  = static_cast<size_t>(index.column());
    const CacheLineSnapshot &line = m_snapshot.at(set, way);
    const bool touched = index.row() == m_touchedRow && index.column() == m_touchedColumn;

    switch (role)
    {
    case Qt::DisplayRole:
        if (!line.valid)
            return QStringLiteral("-");
        return QStringLiteral("0x%1 V%2").arg(line.tag, 0, 16).arg(line.dirty ? " D" : "");
    case Qt::BackgroundRole:
        if (touched)
            return m_last.hit ? ThemeManager::getInstance().getCacheHitColor()
                              : ThemeManager::getInstance().getCacheMissColor();
        return {};
    case Qt::ToolTipRole:
        if (!line.valid)
            return QStringLiteral("Set %1, way %2: empty").arg(set).arg(way);
        return QStringLiteral("Set %1, way %2: tag 0x%3%4\n%5")
            .arg(set)
            .arg(way)
            .arg(line.tag, 0, 16)
            .arg(line.dirty ? QStringLiteral(", dirty") : QString())
            .arg(rankText(set, way));
    case Qt::TextAlignmentRole:
        return Qt::AlignCenter;
    default:
        return {};
    }
}

QString CacheSetModel::rankText(size_t set, size_t way) const
{
    const bool fifo = m_snapshot.replacement == ReplacementPolicy::FIFO;
    const bool lru  = m_snapshot.replacement == ReplacementPolicy::LRU;
    const auto key  = [&](const CacheLineSnapshot &l) { return fifo ? l.insertTime : l.lastAccess; };
    if (!fifo && !lru)
        return QStringLiteral("last used at %1, filled at %2")
            .arg(m_snapshot.at(set, way).lastAccess)
            .arg(m_snapshot.at(set, way).insertTime);

    int valid = 0, older = 0;
    for (size_t w = 0; w < m_snapshot.ways; ++w)
    {
        const CacheLineSnapshot &other = m_snapshot.at(set, w);
        if (!other.valid)
            continue;
        ++valid;
        if (key(other) < key(m_snapshot.at(set, way)))
            ++older;
    }
    // Rank 1 is the line the policy would replace next.
    return QStringLiteral("%1 order: %2 of %3%4")
        .arg(fifo ? QStringLiteral("FIFO") : QStringLiteral("LRU"))
        .arg(older + 1)
        .arg(valid)
        .arg(older == 0 ? QStringLiteral(" (replaced next)") : QString());
}

QVariant CacheSetModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    return orientation == Qt::Horizontal ? QStringLiteral("Way %1").arg(section)
                                         : QStringLiteral("Set %1").arg(section);
}

void CacheSetModel::setState(const CacheSnapshot &snapshot, const CacheAccessRecord &last)
{
    const bool reshaped = snapshot.sets != m_snapshot.sets || snapshot.ways != m_snapshot.ways;
    if (reshaped)
        beginResetModel();
    m_snapshot = snapshot;
    m_last     = last;

    m_touchedRow    = -1;
    m_touchedColumn = -1;
    if (last.sequence != 0 && last.setIndex < m_snapshot.sets)
        for (size_t way = 0; way < m_snapshot.ways; ++way)
        {
            const CacheLineSnapshot &line = m_snapshot.at(last.setIndex, way);
            if (line.valid && line.tag == last.tag)
            {
                m_touchedRow    = static_cast<int>(last.setIndex);
                m_touchedColumn = static_cast<int>(way);
            }
        }

    if (reshaped)
        endResetModel();
    else if (rowCount() > 0 && columnCount() > 0)
        // The view repaints only the visible part of this range.
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

QModelIndex CacheSetModel::touchedIndex() const
{
    return m_touchedRow < 0 ? QModelIndex() : index(m_touchedRow, m_touchedColumn);
}
} // namespace Atlas
