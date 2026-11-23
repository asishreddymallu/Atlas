#include "ui/cache_tab/cache_live_observer.h"

namespace Atlas
{
CacheLiveObserver::CacheLiveObserver(QObject *parent) : QObject(parent) {}

void CacheLiveObserver::attach(Cache *cache)
{
    disconnect(m_hit);
    disconnect(m_miss);
    m_cache = cache;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state.log.clear();
    }
    if (m_cache)
    {
        // Direct: the handler runs on the accessing thread, before that
        // thread can make the next access.
        m_hit  = connect(m_cache, &Cache::cacheHitSignal, this, [this] { onAccess(); },
                         Qt::DirectConnection);
        m_miss = connect(m_cache, &Cache::cacheMissSignal, this, [this] { onAccess(); },
                         Qt::DirectConnection);
    }
    resync();
}

void CacheLiveObserver::resync()
{
    CacheSnapshot snapshot = m_cache ? m_cache->snapshot() : CacheSnapshot{};
    std::lock_guard<std::mutex> lock(m_mutex);
    const bool geometryChanged = snapshot.sets != m_state.snapshot.sets ||
                                 snapshot.ways != m_state.snapshot.ways ||
                                 snapshot.lineBytes != m_state.snapshot.lineBytes;
    if (geometryChanged)
        m_state.log.clear(); // records for another geometry would point at the wrong rows
    m_state.snapshot = std::move(snapshot);
    ++m_state.version;
}

void CacheLiveObserver::copySet(size_t setIndex)
{
    // Caller holds m_mutex. Skipped if a reconfigure has not been resynced
    // yet: the shadow then has another geometry.
    const CacheSnapshot &shadow = m_state.snapshot;
    if (setIndex >= shadow.sets || shadow.sets != m_cache->getSetCount() ||
        shadow.ways != m_cache->getWayCount())
        return;
    for (size_t way = 0; way < shadow.ways; ++way)
    {
        const CacheLine &line = m_cache->getCacheLine(setIndex, way);
        m_state.snapshot.lines[setIndex * shadow.ways + way] = {line.valid, line.dirty, line.tag,
                                                                line.lastAccess, line.insertTime};
    }
}

void CacheLiveObserver::onAccess()
{
    if (!m_cache)
        return;
    const CacheAccessRecord record = m_cache->lastAccess();
    std::lock_guard<std::mutex> lock(m_mutex);
    copySet(record.setIndex);
    if (record.size > 1)
    {
        const size_t last = m_cache->getSetIndex(record.address + record.size - 1);
        if (last != record.setIndex)
            copySet(last);
    }
    m_state.log.push_back(record);
    while (m_state.log.size() > kLogSize)
        m_state.log.pop_front();
    ++m_state.version;
}

CacheLiveObserver::State CacheLiveObserver::state() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

uint64_t CacheLiveObserver::version() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state.version;
}

QString CacheLiveObserver::describe(const CacheAccessRecord &record)
{
    if (record.sequence == 0)
        return QStringLiteral("No access yet");
    QString text = QStringLiteral("%1 0x%2 -> set %3, tag 0x%4: %5")
                       .arg(record.isWrite ? QStringLiteral("WRITE") : QStringLiteral("READ"))
                       .arg(record.address, 8, 16, QLatin1Char('0'))
                       .arg(record.setIndex)
                       .arg(record.tag, 0, 16)
                       .arg(record.hit ? QStringLiteral("HIT") : QStringLiteral("MISS"));
    if (record.evicted)
        text += QStringLiteral(", evicted tag 0x%1").arg(record.evictedTag, 0, 16);
    return text;
}
} // namespace Atlas
