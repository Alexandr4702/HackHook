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
    size_t pat_len;
    std::array<size_t, 256> skip;

    SimdBmhAvx2Searcher(const uint8_t *pattern, size_t length) : pat(pattern), pat_len(length)
    {
        skip.fill(pat_len);
        for (size_t i = 0; i + 1 < pat_len; ++i)
            skip[pat[i]] = pat_len - 1 - i;
    }

    SimdBmhAvx2Searcher(const char *pattern_str)
        : pat(reinterpret_cast<const uint8_t *>(pattern_str)), pat_len(strlen(pattern_str))
    {
        skip.fill(pat_len);
        for (size_t i = 0; i + 1 < pat_len; ++i)
            skip[pat[i]] = pat_len - 1 - i;
    }
};

MYHOOK_API std::vector<size_t> bmh_simd_avx2_all_extended(const uint8_t *hay, size_t hay_len, const SimdBmhAvx2Searcher &searcher);
MYHOOK_API std::vector<size_t> bmh_simd_avx2_all_extended(const uint8_t *hay, size_t hay_len, const uint8_t *pat, size_t pat_len);

#endif // BHM_SIMD_H
