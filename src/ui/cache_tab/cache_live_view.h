/**
 * @file cache_live_view.h
 * @brief The live cache-line view in one Cache sub-tab: the set x way table,
 *        the last access, and the last 16 accesses.
 */
#pragma once

#include "ui/cache_tab/cache_live_observer.h"

#include <QWidget>

class QLabel;
class QListWidget;
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
    QStringList    logLines() const;

  private:
    CacheSetModel *m_model{nullptr};
    QTableView    *m_table{nullptr};
    QLabel        *m_lastAccess{nullptr};
    QListWidget   *m_log{nullptr};
};
} // namespace Atlas
