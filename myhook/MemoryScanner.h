#ifndef BOYERMOOREHORSPOOL_H
#define BOYERMOOREHORSPOOL_H

#include <vector>
#include <tuple>
#include <cstdint>
#include <span>

struct FoundOccurrences
{
    uint64_t baseAddress;
    uint64_t offset;
    uint64_t region_size;
    uint64_t data_size;
    int32_t type;
};

std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern);


#endif // BOYERMOOREHORSPOOL_H
