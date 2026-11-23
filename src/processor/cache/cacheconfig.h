#include <cstddef>
#include <QMetaType>
#pragma once

namespace Atlas
{
enum class WritePolicy
{
    WriteThrough = 0,
    WriteBack
};

enum class AllocationPolicy
{
    WriteAllocate = 0,
    NoWriteAllocate
};

enum class ReplacementPolicy 
{
    LRU = 0,
    FIFO,
    Custom
};

struct CacheConfig
{
    size_t lineCount;
    size_t lineSizeInBytes;
    size_t wayCount;
    WritePolicy writePolicy;
    AllocationPolicy allocationPolicy;
    ReplacementPolicy replacementPolicy;
};

struct CacheStats
{
    size_t hitCount         = 0;
    size_t missCount        = 0;
    size_t writeBackCount   = 0;
    size_t evictionCount    = 0; // fills that replaced a valid line
    double hitRate          = 0.0;
    size_t cacheSizeInBytes = 0;
};

}//namespace Atlas

//for making the enums usable in Qt's combo box
Q_DECLARE_METATYPE(Atlas::WritePolicy)
Q_DECLARE_METATYPE(Atlas::AllocationPolicy)
Q_DECLARE_METATYPE(Atlas::ReplacementPolicy)