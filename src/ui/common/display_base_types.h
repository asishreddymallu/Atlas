#pragma once

namespace Atlas
{
enum class Base
{
    Hexadecimal = 16,
    Decimal = 10,
    Binary = 2
};

/**
 * @brief The order the Memory tab reads a multi-byte quantity in.
 *
 * A DISPLAY choice, and only that. Memory holds bytes; the simulator
 * assembles them little-endian and always will - `MainMemory::readGeneric`
 * (`main_memory.cpp:79`) and `Cache::readGeneric` (`cache.cpp:514`) are
 * not parameterised by this and must not become so. This says how the
 * Double Word column spells those same bytes back to the reader, and it
 * reaches exactly one place: `MemoryModel::data`.
 *
 * It lives here beside `Base` because it is the same kind of thing - how
 * a number is written, not what the number is.
 */
enum class ByteOrder
{
    Little,
    Big
};
} // namespace Atlas
