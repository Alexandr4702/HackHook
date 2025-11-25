#ifndef BOYERMOOREHORSPOOL_H
#define BOYERMOOREHORSPOOL_H

#include <vector>
#include <tuple>
#include <cstdint>
#include <span>
#include <functional>
#include "BMH_SIMD.h"

struct FoundOccurrences
{
    uint64_t baseAddress;
    uint64_t offset;
    uint64_t region_size;
    uint64_t data_size;
    int32_t type;
};

std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern);
MYHOOK_API std::vector<size_t> find_all(
    std::span<const uint8_t> haystack,
    const std::boyer_moore_horspool_searcher<const uint8_t*>& searcher);

#endif // BOYERMOOREHORSPOOL_H
