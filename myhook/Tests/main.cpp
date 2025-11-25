#include <chrono>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <immintrin.h>
#include <iostream>
#include <random>
#include <vector>
#include "BMH_SIMD.h"
#include "MemoryScanner.h"

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

    std::vector<size_t> find_all_matches = find_all(
        std::span<const uint8_t>(text.data(), text.size()),
        searcher
    );

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