/**
 * @file cache_set_model.h
 * @brief One row per set, one column per way: what each line holds, from a
 *        CacheLiveObserver snapshot (never from the cache directly).
 *
 * Cells read "0x1F V D" (tag in hex, then the valid and dirty flags) or "-"
 * for an invalid line. The line the last access touched is painted with the
 * theme's cacheHit or cacheMiss colour. The model only answers data() for
 * the cells a view asks about, so a cache of thousands of sets costs what
 * is on screen.
 */
#pragma once

#include "processor/cache/cache.h"

#include <QAbstractTableModel>

namespace Atlas
{
class CacheSetModel : public QAbstractTableModel
{
    Q_OBJECT
  public:
    explicit CacheSetModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    /// Replaces what is shown. `last` may be a default record (no access yet).
    void setState(const CacheSnapshot &snapshot, const CacheAccessRecord &last);

    /// The (set, way) the last access left its line in, or (-1, -1): a miss
    /// that allocated nothing (a no-write-allocate store) touches no line.
    QModelIndex touchedIndex() const;
    const CacheSnapshot &snapshot() const { return m_snapshot; }

  private:
    /// 1 = next to be replaced ... ways = most recently kept, by the policy's own field.
    QString rankText(size_t set, size_t way) const;

    CacheSnapshot     m_snapshot;
    CacheAccessRecord m_last;
    int               m_touchedRow{-1};
    int               m_touchedColumn{-1};
};
} // namespace Atlas
