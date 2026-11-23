/**
 * @file cache_live_observer.h
 * @brief Watches one Cache for the live cache-line view, without ever reading
 *        it from the wrong thread.
 *
 * A Run executes on the worker thread, so the GUI must not read the cache's
 * lines while one is in progress. Instead, every access is observed where it
 * happens: the observer is connected to the cache's hit/miss signals with
 * Qt::DirectConnection, and on the accessing thread it copies the cache's
 * passive last-access record into a 16-entry log and copies only the set (or
 * the two sets) the access touched into a shadow snapshot, under a mutex.
 * The GUI reads that shadow, never the cache, during a run.
 *
 * A full copy (resync()) is taken only on the GUI thread at moments the
 * cache cannot be changing: after a reconfigure, a load, a manual step, an
 * undo or a finished run. Everything here is read-only: the cache is only
 * ever asked for const views.
 */
#pragma once

#include "processor/cache/cache.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <deque>
#include <mutex>

namespace Atlas
{
class CacheLiveObserver : public QObject
{
    Q_OBJECT
  public:
    static constexpr size_t kLogSize = 16;

    explicit CacheLiveObserver(QObject *parent = nullptr);

    /// Watches `cache` (nullptr stops watching) and resyncs. GUI thread, cache idle.
    void attach(Cache *cache);
    /// A full copy of the cache's lines. GUI thread, only while the cache is idle.
    void resync();

    struct State
    {
        CacheSnapshot snapshot;
        std::deque<CacheAccessRecord> log; ///< oldest first, at most kLogSize
        uint64_t version{0};               ///< changes whenever anything above does
    };
    /// A consistent copy, safe from any thread.
    State state() const;
    uint64_t version() const;

    /// "READ 0x00010040 -> set 4, tag 0x4: MISS, evicted tag 0x3".
    static QString describe(const CacheAccessRecord &record);

  private:
    /// Runs on the thread that made the access, right after it.
    void onAccess();
    void copySet(size_t setIndex);

    // A processor switch destroys the caches before the new ones are
    // attached, and announces the switch in between: a QPointer turns null
    // with its cache, so a resync then sees no cache rather than a freed one.
    QPointer<Cache> m_cache;
    QMetaObject::Connection m_hit, m_miss;
    mutable std::mutex m_mutex;
    State m_state;
};
} // namespace Atlas
