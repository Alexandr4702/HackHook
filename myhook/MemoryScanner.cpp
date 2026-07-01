#include "BMH_SIMD.h"
#include "MyHook.h"

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

namespace
{
// Jmp point for veh fail
thread_local jmp_buf g_jump;
thread_local volatile bool g_jump_armed = false;

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
    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&GetMyDllRegion), &hModule))
    {
        return {nullptr, nullptr};
    }

    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi)))
    {
        return {nullptr, nullptr};
    }

    Region r;
    r.start = reinterpret_cast<uint8_t *>(mi.lpBaseOfDll);
    r.end = r.start + mi.SizeOfImage;
    return r;
}

LONG CALLBACK veh_handler(EXCEPTION_POINTERS *e)
{
    if (e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && g_jump_armed)
    {
        g_jump_armed = false;
        longjmp(g_jump, 1);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

Region GetCurrentThreadStackRegion()
{
    // Get stack region
    auto teb = (NT_TIB *)NtCurrentTeb();
    Region currentThreadStack;
    currentThreadStack.start =
        std::min(reinterpret_cast<uint8_t *>(teb->StackLimit), reinterpret_cast<uint8_t *>(teb->StackBase));
    currentThreadStack.end =
        std::max(reinterpret_cast<uint8_t *>(teb->StackLimit), reinterpret_cast<uint8_t *>(teb->StackBase));
    return currentThreadStack;
}
} // namespace

std::pmr::vector<FoundOccurrences> MemTool::find(std::span<const uint8_t> pattern,
                                                 std::pmr::synchronized_pool_resource &pool,
                                                 std::pmr::vector<Region> &&exludeReg)
{
    std::pmr::vector<FoundOccurrences> result(&pool);
    std::vector<MEMORY_BASIC_INFORMATION> regions = enum_regions();
    std::vector<Region> excludedRegions;

    excludedRegions.push_back(GetCurrentThreadStackRegion());
    excludedRegions.push_back(GetMyDllRegion());
    excludedRegions.insert(excludedRegions.end(), exludeReg.begin(), exludeReg.end());

    // Get page size
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    size_t page_size = si.dwPageSize;
    size_t allocation_granularity = si.dwAllocationGranularity;

    auto number_of_threads = std::max(1u, std::thread::hardware_concurrency());
    std::vector<std::thread> thread_pool;
    thread_pool.reserve(number_of_threads);
    std::vector<std::vector<FoundOccurrences>> results(number_of_threads);
    std::atomic_size_t regions_cnt = 0;
    std::latch waitForAddingStacks(number_of_threads);
    std::counting_semaphore<1024> startThreads(0);

    std::vector<Region> thread_stack_region(number_of_threads);
    std::atomic_bool worker_failed = false;

    const std::boyer_moore_horspool_searcher<const uint8_t *> searcher(pattern.data(), pattern.data() + pattern.size());
    const SimdBmhAvx2Searcher searcher_Avx2(pattern.data(), pattern.size());

    auto worker = [&regions, &searcher, &searcher_Avx2, &pattern, &results, &excludedRegions, &waitForAddingStacks,
                   &startThreads, &regions_cnt, &thread_stack_region, &worker_failed](int threadId) {
        thread_stack_region[threadId] = GetCurrentThreadStackRegion();
        waitForAddingStacks.count_down();
        startThreads.acquire();

        try
        {
            std::vector<FoundOccurrences> &local = results[threadId];
            while (true)
            {
                size_t i = regions_cnt.fetch_add(1, std::memory_order_relaxed);
                if (i >= regions.size())
                    return;
                const MEMORY_BASIC_INFORMATION &region = regions[i];

                if (setjmp(g_jump) != 0)
                    continue;
                g_jump_armed = true;

                std::vector<size_t> matches = bmh_simd_avx2_all_extended(
                    reinterpret_cast<const uint8_t *>(region.BaseAddress), region.RegionSize, searcher_Avx2);
                g_jump_armed = false;

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
        }
        catch (...)
        {
            g_jump_armed = false;
            worker_failed.store(true, std::memory_order_release);
        }
    };

    size_t started_threads = 0;
    for (; started_threads < number_of_threads; ++started_threads)
    {
        try
        {
            thread_pool.emplace_back(worker, static_cast<int>(started_threads));
        }
        catch (...)
        {
            break;
        }
    }
    if (started_threads < number_of_threads)
        waitForAddingStacks.count_down(number_of_threads - started_threads);
    waitForAddingStacks.wait();
    bool setup_failed = false;
    try
    {
        excludedRegions.insert(excludedRegions.end(), thread_stack_region.begin(), thread_stack_region.end());

        std::erase_if(regions, [&excludedRegions](MEMORY_BASIC_INFORMATION &region) {
            if (region.State != MEM_COMMIT)
                return true;
            bool readable =
                (region.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) &&
                !(region.Protect & PAGE_GUARD);

            bool executable = region.Protect & (PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE);
            if (!readable || executable)
                return true;

            uint8_t *baseAddress = reinterpret_cast<uint8_t *>(region.BaseAddress);
            Region currentRegion(baseAddress, baseAddress + region.RegionSize);

            for (auto it = excludedRegions.begin(); it < excludedRegions.end(); ++it)
            {
                if (it->crosses(currentRegion))
                    return true;
            }

            return false;
        });
    }
    catch (...)
    {
        setup_failed = true;
    }

    if (started_threads != 0)
        startThreads.release(started_threads);
    for (auto &thread : thread_pool)
    {
        thread.join();
    }

    if (setup_failed || worker_failed.load(std::memory_order_acquire))
        return result;

    for (size_t threadId = 0; threadId < started_threads; ++threadId)
    {
        result.insert(result.end(), std::make_move_iterator(results[threadId].begin()),
                      std::make_move_iterator(results[threadId].end()));
    }

    return result;
}

size_t MemTool::read(uintptr_t address, void *out, size_t size)
{
    if (!out || size == 0)
        return 0;

    auto *src = reinterpret_cast<const std::byte *>(address);
    auto *dst = reinterpret_cast<std::byte *>(out);

    size_t totalRead = 0;

    while (totalRead < size)
    {
        MEMORY_BASIC_INFORMATION mbi{};

        if (VirtualQuery(src, &mbi, sizeof(mbi)) != sizeof(mbi))
            break;

        if (mbi.State != MEM_COMMIT)
            break;

        const DWORD protect = mbi.Protect & 0xff;

        constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

        if ((protect & readable) == 0)
            break;

        if (protect & (PAGE_GUARD | PAGE_NOACCESS))
            break;

        const auto regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;

        const size_t chunk = std::min(size - totalRead, regionEnd - reinterpret_cast<uintptr_t>(src));

        if (setjmp(g_jump) != 0)
            break;

        g_jump_armed = true;
        std::memcpy(dst, src, chunk);
        g_jump_armed = false;

        totalRead += chunk;
        src += chunk;
        dst += chunk;
    }

    return totalRead;
}

bool MemTool::write(uintptr_t address, const void *data, size_t size)
{
    if (!data || size == 0)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};

    if (VirtualQuery(reinterpret_cast<void *>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;

    if (mbi.State != MEM_COMMIT)
        return false;

    const bool writable =
        (mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY)) && !(mbi.Protect & PAGE_GUARD);

    if (!writable)
        return false;

    const auto region_start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    const auto region_end = region_start + mbi.RegionSize;
    if (address < region_start || address >= region_end || size > region_end - address)
        return false;

    if (setjmp(g_jump) != 0)
        return false;

    g_jump_armed = true;
    std::memcpy(reinterpret_cast<void *>(address), data, size);
    g_jump_armed = false;
    return true;
}

MemTool::MemTool()
{
    m_veHandle = AddVectoredExceptionHandler(1, veh_handler);
}

MemTool::~MemTool()
{
    if (m_veHandle)
        RemoveVectoredExceptionHandler(m_veHandle);
}
