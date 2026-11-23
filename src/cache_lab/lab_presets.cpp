#include "cache_lab/lab_presets.h"

namespace Atlas
{
namespace
{
constexpr auto WT  = WritePolicy::WriteThrough;
constexpr auto WB  = WritePolicy::WriteBack;
constexpr auto WNA = AllocationPolicy::NoWriteAllocate;
constexpr auto WA  = AllocationPolicy::WriteAllocate;
constexpr auto LRU  = ReplacementPolicy::LRU;
constexpr auto FIFO = ReplacementPolicy::FIFO;
} // namespace

const std::vector<LabPreset> &labPresets()
{
    static const std::vector<LabPreset> presets = {
        {"dm256", 4, 0, 2, WT, WNA, LRU,
         "256 B direct-mapped: 16 lines of 16 B (the Lab 4 base setup)."},
        {"dm1024", 6, 0, 2, WT, WNA, LRU,
         "1 KB direct-mapped: 64 lines of 16 B, four times the base capacity."},
        {"dm128", 3, 0, 2, WT, WNA, LRU,
         "128 B direct-mapped: 8 lines of 16 B, half the base capacity."},
        {"2w128-fifo", 3, 1, 1, WT, WNA, FIFO,
         "128 B 2-way set-associative: 8 sets of two 8 B lines, FIFO replacement."},
        {"dm256-32B", 3, 0, 3, WT, WNA, LRU,
         "256 B direct-mapped with longer lines: 8 lines of 32 B (Lab 4 longer lines)."},
        {"2w256-lru", 3, 1, 2, WT, WNA, LRU,
         "256 B 2-way set-associative: 8 sets of two 16 B lines, LRU replacement."},
        {"2w256-fifo", 3, 1, 2, WT, WNA, FIFO,
         "256 B 2-way set-associative: 8 sets of two 16 B lines, FIFO replacement."},
        {"4w256-lru", 2, 2, 2, WT, WNA, LRU,
         "256 B 4-way set-associative: 4 sets of four 16 B lines, LRU replacement."},
        {"2w256-lru-wb", 3, 1, 2, WB, WA, LRU,
         "256 B 2-way LRU, write back and write allocate: stores stay in the cache."},
        {"2w256-fifo-wb", 3, 1, 2, WB, WA, FIFO,
         "256 B 2-way FIFO, write back and write allocate: stores stay in the cache."},
        {"4w512-wb", 2, 2, 3, WB, WA, LRU,
         "512 B 4-way LRU with 32 B lines, write back and write allocate (verification set)."},
    };
    return presets;
}

const LabPreset *findLabPreset(std::string_view name)
{
    for (const LabPreset &preset : labPresets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}
} // namespace Atlas
