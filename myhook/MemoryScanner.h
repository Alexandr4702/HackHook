#ifndef BOYERMOOREHORSPOOL_H
#define BOYERMOOREHORSPOOL_H

#include <vector>
#include <cstdint>
#include <span>
#include <functional>
#include <memory_resource>
#include "MyHookImport.h"

struct FoundOccurrences
{
    uint64_t baseAddress;
    uint64_t offset;
    uint64_t region_size;
    uint64_t data_size;
    int32_t type;
};

struct Region
{
    uint8_t *start;
    uint8_t *end;
    auto operator<=>(const Region &b) const noexcept
    {
        return start == b.start ? end <=> b.end : start <=> b.start;
    }

    bool crosses(const Region &b) const noexcept
    {
        return !(end <= b.start || start >= b.end);
    }
};

// std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern);
std::pmr::vector<FoundOccurrences> find(std::span<const uint8_t> patter, std::pmr::synchronized_pool_resource& pool, Region exludeReg);
#endif // BOYERMOOREHORSPOOL_H
