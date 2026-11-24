/**
 * @file cache_live_view.h
 * @brief The live cache-line view in one Cache sub-tab: the set x way table,
 *        the last access, and the last 16 accesses.
 */
#pragma once

#include "ui/cache_tab/cache_live_observer.h"

#include <QWidget>

class QLabel;
class QListView;
class QStringListModel;
class QTableView;

namespace Atlas
{
class CacheSetModel;

class CacheLiveView : public QWidget
{
    Q_OBJECT
  public:
    explicit CacheLiveView(QWidget *parent = nullptr);

    void showState(const CacheLiveObserver::State &state);

    CacheSetModel *model() const { return m_model; }
    QTableView    *table() const { return m_table; }
    QString        lastAccessText() const;
    /// "Misses by cause [Modelled]: compulsory N \u00b7 capacity N \u00b7 conflict N" (metric 6).
    QString        missCausesText() const;
    QStringList    logLines() const;

  private:
    CacheSetModel *m_model{nullptr};
    QTableView    *m_table{nullptr};
    QLabel        *m_lastAccess{nullptr};
    QLabel        *m_missCauses{nullptr};
    // A QListView over a model rather than a QListWidget: the window is
    // checked to hold no QListWidget (the retired sidebar, ribbon tests).
    QListView        *m_log{nullptr};
    QStringListModel *m_logModel{nullptr};
};
} // namespace Atlas
