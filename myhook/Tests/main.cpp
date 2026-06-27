#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <immintrin.h>
#include <iostream>
#include <random>
#include <vector>
#include <windows.h>
#include "BMH_SIMD.h"
#include "MemoryScanner.h"

namespace
{
class GuardedBuffer
{
  public:
    explicit GuardedBuffer(size_t page_size) : m_pageSize(page_size)
    {
        m_memory = static_cast<uint8_t *>(
            VirtualAlloc(nullptr, m_pageSize * 2, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        if (!m_memory)
            return;

        DWORD old_protect = 0;
        if (!VirtualProtect(m_memory + m_pageSize, m_pageSize, PAGE_NOACCESS, &old_protect))
        {
            VirtualFree(m_memory, 0, MEM_RELEASE);
            m_memory = nullptr;
        }
    }

    ~GuardedBuffer()
    {
        if (m_memory)
            VirtualFree(m_memory, 0, MEM_RELEASE);
    }

    GuardedBuffer(const GuardedBuffer &) = delete;
    GuardedBuffer &operator=(const GuardedBuffer &) = delete;

    uint8_t *tail(size_t size) const
    {
        return size <= m_pageSize && m_memory ? m_memory + m_pageSize - size : nullptr;
    }

    bool valid() const
    {
        return m_memory != nullptr;
    }

  private:
    uint8_t *m_memory = nullptr;
    size_t m_pageSize = 0;
};
} // namespace

TEST(BMH_SIMD, DoesNotReadPastBuffers)
{
    SYSTEM_INFO system_info{};
    GetSystemInfo(&system_info);

    GuardedBuffer hay_buffer(system_info.dwPageSize);
    GuardedBuffer pattern_buffer(system_info.dwPageSize);
    ASSERT_TRUE(hay_buffer.valid());
    ASSERT_TRUE(pattern_buffer.valid());

    constexpr std::array<size_t, 14> lengths{1, 2, 15, 16, 17, 31, 32, 33, 63, 64, 65, 95, 96, 97};
    for (const size_t length : lengths)
    {
        uint8_t *hay = hay_buffer.tail(length);
        uint8_t *pattern = pattern_buffer.tail(length);
        std::memset(hay, 0x5A, length);
        std::memset(pattern, 0x5A, length);

        const SimdBmhAvx2Searcher searcher(pattern, length);
        EXPECT_EQ(bmh_simd_avx2_all_extended(hay, length, searcher), (std::vector<size_t>{0}))
            << "length=" << length;

        pattern[length / 2] ^= 0xFF;
        const SimdBmhAvx2Searcher mismatch_searcher(pattern, length);
        EXPECT_TRUE(bmh_simd_avx2_all_extended(hay, length, mismatch_searcher).empty()) << "length=" << length;
    }
}

TEST(BMH_SIMD_BENCH, CompareWithFindAll)
{
    const size_t TEXT_SIZE = 50'000'000;
    const std::string PATTERN = "patternpatternpattern";
    const int NUM_INSERTS = 1000;

    // generate random text
    std::vector<uint8_t> text(TEXT_SIZE);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    for (size_t i = 0; i < TEXT_SIZE; ++i)
        text[i] = dist(rng);

    // insert pattern NUM_INSERTS times
    std::uniform_int_distribution<size_t> pos_dist(0, TEXT_SIZE - PATTERN.size());
    std::vector<size_t> inserted_positions;
    for (int i = 0; i < NUM_INSERTS; ++i) {
        size_t pos = pos_dist(rng);
        std::memcpy(&text[pos], PATTERN.data(), PATTERN.size());
        inserted_positions.push_back(pos);
    }

    // ---------------- SIMD-BMH search ----------------
    auto start_simd = std::chrono::high_resolution_clock::now();

    SimdBmhAvx2Searcher skip_table(PATTERN.data());
    auto simd_matches = bmh_simd_avx2_all_extended(
        text.data(), text.size(),
        skip_table
    );

    auto end_simd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> simd_time = end_simd - start_simd;

    std::cout << "[SIMD-BMH] Found " << simd_matches.size() << " matches in "
              << simd_time.count() << " s\n";

    // ---------------- find_all search ----------------
    auto start_find_all = std::chrono::high_resolution_clock::now();

    std::boyer_moore_horspool_searcher<const uint8_t*> searcher(
        reinterpret_cast<const uint8_t*>(PATTERN.data()),
        reinterpret_cast<const uint8_t*>(PATTERN.data()) + PATTERN.size()
    );

    std::vector<size_t> find_all_matches;
    auto search_begin = text.cbegin();
    while (search_begin != text.cend())
    {
        const auto match = std::search(search_begin, text.cend(), searcher);
        if (match == text.cend())
            break;

        find_all_matches.push_back(static_cast<size_t>(std::distance(text.cbegin(), match)));
        search_begin = std::next(match);
    }

    auto end_find_all = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> find_all_time = end_find_all - start_find_all;

    std::cout << "[find_all] Found " << find_all_matches.size() << " matches in "
              << find_all_time.count() << " s\n";

    // ---------------- check correctness ----------------
    int missing_simd = 0;
    for (size_t p : inserted_positions) {
        if (std::find(simd_matches.begin(), simd_matches.end(), p) == simd_matches.end())
            ++missing_simd;
    }
    std::cout << "[SIMD-BMH] missing inserted: " << missing_simd << "\n";

    int missing_find_all = 0;
    for (size_t p : inserted_positions) {
        if (std::find(find_all_matches.begin(), find_all_matches.end(), p) == find_all_matches.end())
            ++missing_find_all;
    }
    std::cout << "[find_all] missing inserted: " << missing_find_all << "\n";
}
