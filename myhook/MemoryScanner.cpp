#include "MemoryScanner.h"
#include "BMH_SIMD.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <algorithm>
#include <immintrin.h> // AVX2

#include <vector>
#include <array>
#include <windows.h>

// Boyer-Moore-Horspool pattern structure with precomputed shift table
struct BMHPattern {
    std::vector<uint8_t> pattern;       // pattern bytes
    std::array<size_t, 256> shift;      // shift table for each possible byte

    explicit BMHPattern(std::span<const uint8_t> p) : pattern(p.begin(), p.end()) {
        size_t len = pattern.size();

        shift.fill(len);  // default shift = pattern length
        for (size_t i = 0; i + 1 < len; ++i) {
            shift[pattern[i]] = len - i - 1;  // compute shifts
        }
    }

    size_t size() const { return pattern.size(); }
    const uint8_t* data() const { return pattern.data(); }
};

std::vector<MEMORY_BASIC_INFORMATION> enum_regions()
{
    std::vector<MEMORY_BASIC_INFORMATION> regions;

    uint8_t *addr = nullptr;
    MEMORY_BASIC_INFORMATION mbi;

    while (VirtualQuery(addr, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        regions.emplace_back(mbi);
        addr += mbi.RegionSize;
    }

    return regions;
}

std::vector<size_t> find_all(
    std::span<const uint8_t> haystack,
    const std::boyer_moore_horspool_searcher<const uint8_t*>& searcher)
{
    std::vector<size_t> matches;
    auto it = haystack.begin();

    while (it != haystack.end()) {
        auto found = std::search(it, haystack.end(), searcher);
        if (found == haystack.end()) break;

        matches.push_back(std::distance(haystack.begin(), found));

        if (found == haystack.end() - 1) break;
        it = found + 1;
    }

    return matches;
}


std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern)
{
    std::vector<FoundOccurrences> result;
    std::vector<MEMORY_BASIC_INFORMATION> regions = enum_regions();
    std::boyer_moore_horspool_searcher<const uint8_t*> searcher(pattern.data(), pattern.data() + pattern.size());
    std::vector<std::future<std::vector<FoundOccurrences>>> features;

    for (auto &region : regions)
    {
        // if (region.State != MEM_COMMIT)
        //     continue;
        bool readable = (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
                !(region.Protect & PAGE_GUARD);

        // if ((region.State == MEM_COMMIT) && region.Protect != PAGE_NOACCESS && !(region.Protect & PAGE_GUARD))
        if (readable)
        {
            features.emplace_back(std::async(
                std::launch::async,
                [&searcher, &pattern](const MEMORY_BASIC_INFORMATION &region) {

                    std::vector<FoundOccurrences> local;

                    std::vector<size_t> matches = find_all(
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(region.BaseAddress), region.RegionSize),
                        searcher);

                    for (auto &match : matches)
                    {
                        FoundOccurrences found;
                        found.baseAddress = reinterpret_cast<uint64_t> (region.BaseAddress);
                        found.offset = match;
                        found.region_size = region.RegionSize;
                        found.data_size = pattern.size();
                        found.type = region.Type;
                        local.emplace_back(found);
                    }

                    return local;
                },
                region));
        }
    }

    for (auto &f : features)
    {
        auto part = f.get();
        result.insert(result.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }

    return result;
}