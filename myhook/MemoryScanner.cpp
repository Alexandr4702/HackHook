#include "MemoryScanner.h"
#include "BMH_SIMD.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <future>
#include <latch>
#include <semaphore>
#include <span>

#include <vector>
#include <windows.h>
#include <setjmp.h>
#include <psapi.h>

#include <processthreadsapi.h>

// Jmp point for veh fail
thread_local jmp_buf g_jump;

struct Region {
    uint8_t* start;
    uint8_t* end;
    auto operator<=>(const Region& b) const noexcept
    {
        return start == b.start ? end <=> b.end : start <=> b.start; 
    }

    bool crosses(const Region& b) const noexcept
    {
        return !(end <= b.start || start >= b.end);
    }
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

Region GetMyDllRegion()
{
    HMODULE hModule = nullptr;
    if (!GetModuleHandleEx(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&GetMyDllRegion),
            &hModule))
    {
        return {nullptr, nullptr};
    }

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi)))
    {
        return {nullptr, nullptr};
    }

    Region r;
    r.start = reinterpret_cast<uint8_t*>(mi.lpBaseOfDll);
    r.end   = r.start + mi.SizeOfImage;
    return r;
}

std::vector<size_t> find_all(std::span<const uint8_t> haystack,
                             const std::boyer_moore_horspool_searcher<const uint8_t *> &searcher)
{
    std::vector<size_t> matches;
    auto it = haystack.begin();

    while (it != haystack.end())
    {
        auto found = std::search(it, haystack.end(), searcher);
        if (found == haystack.end())
            break;

        matches.push_back(std::distance(haystack.begin(), found));

        if (found == haystack.end() - 1)
            break;
        it = found + 1;
    }

    return matches;
}

LONG CALLBACK veh_handler(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION)
    {
        longjmp(g_jump, 1);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

Region GetCurrentThreadStackRegion()
{
    // Get stack region
    auto teb = (NT_TIB*)NtCurrentTeb();
    Region currentThreadStack;
    currentThreadStack.start = std::min(reinterpret_cast<uint8_t*>(teb->StackLimit), reinterpret_cast<uint8_t*>(teb->StackBase));
    currentThreadStack.end   = std::max(reinterpret_cast<uint8_t*>(teb->StackLimit), reinterpret_cast<uint8_t*>(teb->StackBase));
    return currentThreadStack;
}

std::vector<FoundOccurrences> find(std::span<const uint8_t> pattern)
{
    std::vector<FoundOccurrences> result;
    std::vector<MEMORY_BASIC_INFORMATION> regions = enum_regions();
    std::vector<std::future<std::vector<FoundOccurrences>>> features;
    std::vector<Region> excludedRegions;

    excludedRegions.push_back(GetCurrentThreadStackRegion());
    excludedRegions.push_back(GetMyDllRegion());

    // Get page size
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t page_size = si.dwPageSize;
    size_t allocation_granularity = si.dwAllocationGranularity;

    auto number_of_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> thread_pool;
    std::vector<std::vector<FoundOccurrences>> results(number_of_threads);
    std::mutex mtx;
    std::atomic_size_t regions_cnt = 0;
    std::latch waitForAddingStacks(number_of_threads);
    std::counting_semaphore<1024> startThreads(0);

    const std::boyer_moore_horspool_searcher<const uint8_t *> searcher(pattern.data(), pattern.data() + pattern.size());
    const SimdBmhAvx2Searcher searcher_Avx2(pattern.data(), pattern.size());

    PVOID veh_handle = AddVectoredExceptionHandler(1, veh_handler);

    auto worker = [&regions, &mtx, &searcher, &searcher_Avx2, &pattern, &results, &excludedRegions,
                   &waitForAddingStacks, &startThreads, &regions_cnt](int threadId) {
        {
            std::unique_lock lck(mtx);
            excludedRegions.push_back(GetCurrentThreadStackRegion());
        }
        waitForAddingStacks.count_down();
        startThreads.acquire();

        std::vector<FoundOccurrences> &local = results[threadId];
        while (true)
        {
            size_t i = regions_cnt.fetch_add(1, std::memory_order_relaxed);
            if (i >= regions.size())
                return;
            const MEMORY_BASIC_INFORMATION& region = regions[i];

            if (setjmp(g_jump) != 0)
            {
                continue;
            }

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

    waitForAddingStacks.wait();

    std::sort(excludedRegions.begin(), excludedRegions.end());
    std::erase_if(regions, [&excludedRegions](MEMORY_BASIC_INFORMATION &region) {
        if (region.State != MEM_COMMIT)
            return true;
        bool readable =
            (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
            !(region.Protect & PAGE_GUARD);

        bool executable = region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE);
        if (!readable || executable)
        {
            return true;
        }

        uint8_t *baseAddress = reinterpret_cast<uint8_t *>(region.BaseAddress);
        Region currentRegion(baseAddress, baseAddress + region.RegionSize);
        auto it = std::lower_bound(excludedRegions.begin(), excludedRegions.end(), currentRegion);
        if (it != excludedRegions.begin())
        {
            auto prev = std::prev(it);
            if (prev->crosses(currentRegion))
                return true;
        }

        for (; it < excludedRegions.end(); ++it)
        {
            if (it->crosses(currentRegion))
                return true;
            if (it->start >= currentRegion.end)
                break;
        }

        return false;
    });

    startThreads.release(number_of_threads);
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