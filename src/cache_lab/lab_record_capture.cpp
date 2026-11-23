#include "cache_lab/lab_record_capture.h"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>

#include <bit>

namespace Atlas
{
LabRecordCache captureLabRecordCache(const std::string &level, const Cache &cache)
{
    const CacheConfig config = cache.getConfig();
    LabRecordCache    out;
    out.level = level;

    out.sets      = cache.getSetCount();
    out.ways      = cache.getWayCount();
    out.lineBytes = cache.getLineSizeInBytes();
    out.totalBytes = cache.getCacheSizeInBytes();
    out.setsExponent  = std::countr_zero(out.sets);
    out.waysExponent  = std::countr_zero(out.ways);
    out.wordsExponent = std::countr_zero(out.lineBytes / 4);

    out.writeHit  = config.writePolicy == WritePolicy::WriteBack ? "write-back" : "write-through";
    out.writeMiss = config.allocationPolicy == AllocationPolicy::WriteAllocate ? "write-allocate"
                                                                               : "no-write-allocate";
    out.replacement = config.replacementPolicy == ReplacementPolicy::LRU    ? "lru"
                      : config.replacementPolicy == ReplacementPolicy::FIFO ? "fifo"
                                                                            : "custom";

    out.hits       = cache.getHitCount();
    out.misses     = cache.getMissCount();
    out.evictions  = cache.getEvictionCount();
    out.writeBacks = cache.getWriteBackCount();
    return out;
}

std::string sha256Hex(const std::string &source)
{
    return QCryptographicHash::hash(QByteArray::fromStdString(source), QCryptographicHash::Sha256)
        .toHex()
        .toStdString();
}

std::string utcTimestamp()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
}
} // namespace Atlas
