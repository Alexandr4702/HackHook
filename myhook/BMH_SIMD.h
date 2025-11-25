#ifndef BHM_SIMD_H
#define BHM_SIMD_H

#include <array>
#include <cstdint>
#include <cstring>
#include <immintrin.h>
#include <vector>

#ifdef _WIN32
    #ifdef MYHOOK_BUILD
        #define MYHOOK_API __declspec(dllexport)
    #else
        #define MYHOOK_API __declspec(dllimport)
    #endif
#else
    #define MYHOOK_API
#endif

struct SimdBmhAvx2Searcher
{
    const uint8_t *pat;
    size_t size;
    std::array<size_t, 256> skip;

    SimdBmhAvx2Searcher(const uint8_t *pattern, size_t length) : pat(pattern), size(length)
    {
        skip.fill(size);
        for (size_t i = 0; i + 1 < size; ++i)
            skip[pat[i]] = size - 1 - i;
    }

    SimdBmhAvx2Searcher(const char *pattern_str)
        : pat(reinterpret_cast<const uint8_t *>(pattern_str)), size(strlen(pattern_str))
    {
        skip.fill(size);
        for (size_t i = 0; i + 1 < size; ++i)
            skip[pat[i]] = size - 1 - i;
    }
};

MYHOOK_API std::vector<size_t> bmh_simd_avx2_all_extended(const uint8_t *hay, size_t hay_len, const SimdBmhAvx2Searcher &searcher);

#endif // BHM_SIMD_H
