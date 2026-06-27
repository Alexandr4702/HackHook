#ifndef FOUND_OCCURRENCES_H
#define FOUND_OCCURRENCES_H

#include <compare>
#include <cstdint>

struct FoundOccurrences
{
    uint64_t baseAddress;
    uint64_t offset;
    uint64_t region_size;
    uint64_t data_size;
    int32_t type;

    auto operator<=>(const FoundOccurrences &other) const noexcept
    {
        return (baseAddress + offset) <=> (other.baseAddress + other.offset);
    }
};

#endif
