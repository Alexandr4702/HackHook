#include "MemoryScanner.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <span>
#include <string_view>
#include <algorithm>
#include <functional>

#include <vector>
#include <windows.h>


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
    std::span<const uint8_t> needle)
{
    std::vector<size_t> matches;
    if (needle.empty() || haystack.size() < needle.size())
        return matches;

    auto searcher = std::boyer_moore_horspool_searcher(
        needle.begin(), needle.end());

    auto it = std::search(haystack.begin(), haystack.end(), searcher);
    while (it != haystack.end()) {
        matches.push_back(std::distance(haystack.begin(), it));
        it = std::search(it + 1, haystack.end(), searcher);
    }

    return matches;
}

std::vector<size_t> find_all(
    std::span<const uint8_t> haystack,
    const std::boyer_moore_horspool_searcher<const uint8_t*>& searcher)
{
    std::vector<size_t> matches;

    auto it = std::search(haystack.begin(), haystack.end(), searcher);
    while (it != haystack.end()) {
        matches.push_back(std::distance(haystack.begin(), it));
        it = std::search(it + 1, haystack.end(), searcher);
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

        if ((region.State == MEM_COMMIT) && region.Protect != PAGE_NOACCESS && !(region.Protect & PAGE_GUARD))
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