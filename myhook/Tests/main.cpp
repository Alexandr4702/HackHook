#include <chrono>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <immintrin.h>
#include <iostream>
#include <random>
#include <vector>
#include "BMH_SIMD.h"

TEST(BMH_SIMD_TEST, BMH_SIMD_TEST)
{
    const size_t TEXT_SIZE = 50'000'000;
    const std::string PATTERN = "patternpatternpattern";
    const int NUM_INSERTS = 1000;

    std::vector<uint8_t> text(TEXT_SIZE);
    std::mt19937 rng(42);
    std::uniform_int_distribution<uint8_t> dist(0, 255);
    for (size_t i = 0; i < TEXT_SIZE; ++i)
        text[i] = dist(rng);

    std::uniform_int_distribution<size_t> pos_dist(0, TEXT_SIZE - PATTERN.size());
    std::vector<size_t> inserted_positions;
    for (int i = 0; i < NUM_INSERTS; ++i)
    {
        size_t pos = pos_dist(rng);
        std::memcpy(&text[pos], PATTERN.data(), PATTERN.size());
        inserted_positions.push_back(pos);
    }

    auto start = std::chrono::high_resolution_clock::now();

    SimdBmhAvx2Searcher searcher(PATTERN.data());

    auto matches = bmh_simd_avx2_all_extended(
        reinterpret_cast<const uint8_t*>(text.data()), text.size(),
                                             searcher);
    // std::vector<size_t> matches;
    auto end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> diff = end - start;
    std::cout << "Found " << matches.size() << " matches.\n";
    std::cout << "Expected at least " << NUM_INSERTS << " matches.\n";
    std::cout << "Time taken: " << diff.count() << " s\n";

    int missing = 0;
    for (size_t p : inserted_positions)
    {
        if (std::find(matches.begin(), matches.end(), p) == matches.end())
            ++missing;
    }
    if (missing == 0)
        std::cout << "All inserted patterns found.\n";
    else
        std::cout << missing << " inserted patterns not found!\n";

}

// int main(int argc, char **argv)
// {
//     ::testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }