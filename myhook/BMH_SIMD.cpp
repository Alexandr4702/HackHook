#include "BMH_SIMD.h"

#include <cstddef>
#include <cstring>

// ----------------------------- utility: avx2 memchr-like -----------------------------

static inline size_t find_byte_avx2(const uint8_t *data, size_t len, uint8_t b)
{
    const __m256i key = _mm256_set1_epi8((char)b);
    size_t i = 0;

    while (i + 32 <= len)
    {
        __m256i v = _mm256_loadu_si256((const __m256i *)(data + i));
        __m256i cmp = _mm256_cmpeq_epi8(v, key);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(cmp);
        if (mask)
        {
            return i + (size_t)__builtin_ctz(mask);
        }
        i += 32;
    }

    while (i < len)
    {
        if (data[i] == b)
            return i;
        ++i;
    }
    return len;
}

static inline bool simd_compare_16(const uint8_t *hay, const uint8_t *pat)
{
    const __m128i h = _mm_loadu_si128(reinterpret_cast<const __m128i *>(hay));
    const __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i *>(pat));
    return static_cast<uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(h, p))) == 0xFFFFu;
}

static inline bool simd_compare_32(const uint8_t *hay, const uint8_t *pat)
{
    const __m256i h = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(hay));
    const __m256i p = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(pat));
    return static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(h, p))) == 0xFFFFFFFFu;
}

static inline bool simd_compare_any(const uint8_t *hay, const uint8_t *pat, size_t len)
{
    if (len < 16)
        return std::memcmp(hay, pat, len) == 0;

    if (len < 32)
        return simd_compare_16(hay, pat) && simd_compare_16(hay + len - 16, pat + len - 16);

    if (len < 64)
        return simd_compare_32(hay, pat) && simd_compare_32(hay + len - 32, pat + len - 32);

    size_t off = 0;
    while (len - off >= 32)
    {
        if (!simd_compare_32(hay + off, pat + off))
            return false;
        off += 32;
    }

    return off == len || simd_compare_32(hay + len - 32, pat + len - 32);
}

// ----------------------------- SIMD-BMH (collect ALL matches), extended -----------------------------

// Search all occurrences (including overlapping) of pat in hay using BMH with AVX2-accelerated
// last-byte scanning and SIMD full-match. Supports arbitrary pat_len (>=1).

std::vector<size_t> bmh_simd_avx2_all_extended(const uint8_t *hay, size_t hay_len, const SimdBmhAvx2Searcher &searcher)
{
    const uint8_t *pat = searcher.pat;
    size_t pat_len = searcher.size;
    const auto &skip = searcher.skip;

    std::vector<size_t> result;
    if (pat_len == 0 || hay_len < pat_len)
        return result;

    const uint8_t last = pat[pat_len - 1];
    size_t pos = 0;

    while (pos + pat_len <= hay_len)
    {
        size_t search_start = pos + pat_len - 1;
        size_t remain = hay_len - search_start;

        size_t rel = find_byte_avx2(hay + search_start, remain, last);
        if (rel == remain)
            return result;

        size_t candidate_end = search_start + rel;
        size_t candidate_pos = candidate_end - (pat_len - 1);

        if (simd_compare_any(hay + candidate_pos, pat, pat_len))
        {
            result.push_back(candidate_pos);
            pos = candidate_pos + 1;
            continue;
        }

        pos = candidate_pos + skip[hay[candidate_end]];
    }

    return result;
}
