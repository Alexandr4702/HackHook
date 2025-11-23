#include <immintrin.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <chrono>
#include <random>
#include <gtest/gtest.h>

// insert your bmh_avx512_prefetch_batch implementation here

// int main() {
//     const size_t TEXT_SIZE = 50'000'000;
//     const std::string PATTERN = "patternpatternpattern";
//     const int NUM_INSERTS = 1000;

//     std::vector<uint8_t> text(TEXT_SIZE);
//     std::mt19937 rng(42);
//     std::uniform_int_distribution<uint8_t> dist(0, 255);
//     for (size_t i = 0; i < TEXT_SIZE; ++i)
//         text[i] = dist(rng);

//     std::uniform_int_distribution<size_t> pos_dist(0, TEXT_SIZE - PATTERN.size());
//     std::vector<size_t> inserted_positions;
//     for (int i = 0; i < NUM_INSERTS; ++i) {
//         size_t pos = pos_dist(rng);
//         std::memcpy(&text[pos], PATTERN.data(), PATTERN.size());
//         inserted_positions.push_back(pos);
//     }

//     auto start = std::chrono::high_resolution_clock::now();
//     auto matches = bmh_avx512_prefetch_batch(
//         text.data(), text.size(),
//         reinterpret_cast<const uint8_t*>(PATTERN.data()), PATTERN.size()
//     );
//     auto end = std::chrono::high_resolution_clock::now();

//     std::chrono::duration<double> diff = end - start;
//     std::cout << "Found " << matches.size() << " matches.\n";
//     std::cout << "Expected at least " << NUM_INSERTS << " matches.\n";
//     std::cout << "Time taken: " << diff.count() << " s\n";

//     int missing = 0;
//     for (size_t p : inserted_positions) {
//         if (std::find(matches.begin(), matches.end(), p) == matches.end())
//             ++missing;
//     }
//     if (missing == 0)
//         std::cout << "All inserted patterns found.\n";
//     else
//         std::cout << missing << " inserted patterns not found!\n";

//     return 0;
// }

int add(int a, int b) {
    return a + b;
}

TEST(AdditionTest, Simple) {
    EXPECT_EQ(add(2, 3), 5);
    EXPECT_EQ(add(-1, 1), 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}