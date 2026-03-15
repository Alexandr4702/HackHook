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

LONG CALLBACK veh_handler(EXCEPTION_POINTERS* e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        // Skip the faulting instruction
#ifdef _M_X64
        e->ContextRecord->Rip += 1;
#else
        e->ContextRecord->Eip += 1;
#endif
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern)
{
    std::vector<FoundOccurrences> result;
    std::vector<MEMORY_BASIC_INFORMATION> regions = enum_regions();
    std::vector<std::future<std::vector<FoundOccurrences>>> features;

    std::erase_if(regions, [](MEMORY_BASIC_INFORMATION &region) {
        if (region.State != MEM_COMMIT)
            return true;
        bool readable =
            (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
            !(region.Protect & PAGE_GUARD);
        return !readable;
    });

    auto number_of_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> thread_pool;
    std::vector<std::vector<FoundOccurrences>> results(number_of_threads);
    std::mutex mtx;
    const std::boyer_moore_horspool_searcher<const uint8_t*> searcher(pattern.data(), pattern.data() + pattern.size());
    const SimdBmhAvx2Searcher searcher_Avx2(pattern.data(), pattern.size());

    PVOID veh_handle = AddVectoredExceptionHandler(1, veh_handler);

    auto worker = [&regions, &mtx, &searcher, &searcher_Avx2, &pattern, &results](int threadId) {
        std::vector<FoundOccurrences> &local = results[threadId];
        while (true)
        {
            std::unique_lock lck(mtx);
            if (regions.empty())
                return;
            auto region = regions.back();
            regions.pop_back();
            lck.unlock();

            // std::vector<size_t> matches = find_all(
            //     std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(region.BaseAddress), region.RegionSize),
            //     searcher);

            std::vector<size_t> matches = bmh_simd_avx2_all_extended(
                reinterpret_cast<const uint8_t *>(region.BaseAddress), region.RegionSize, searcher_Avx2);

            for (auto &match : matches)
            {
                FoundOccurrences found;
                found.baseAddress = reinterpret_cast<uint64_t>(region.BaseAddress);
                found.offset = match;
                found.region_size = region.RegionSize;
                found.data_size = pattern.size();
                found.type = region.Type;
                local.emplace_back(found);
            }
        }
    };

    for (int threadId{0}; threadId < number_of_threads; ++threadId)
    {
        thread_pool.push_back(std::thread(worker, threadId));
    }

    for (int threadId{0}; threadId < number_of_threads; ++threadId)
    {
        thread_pool[threadId].join();
    }

    for (int threadId{0}; threadId < number_of_threads; ++threadId)
    {
        result.insert(result.end(), std::make_move_iterator(results[threadId].begin()),
                      std::make_move_iterator(results[threadId].end()));
    }

    RemoveVectoredExceptionHandler(veh_handle);
    return result;
}