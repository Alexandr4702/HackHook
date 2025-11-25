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

// ----------------------------- SIMD full compare 1..64 bytes -----------------------------

static inline bool simd_compare_1_64(const uint8_t *hay, const uint8_t *pat, size_t len)
{
    // len in 1..64
    if (len <= 16)
    {
        __m128i h = _mm_loadu_si128((const __m128i *)hay);
        __m128i p = _mm_loadu_si128((const __m128i *)pat);
        __m128i c = _mm_cmpeq_epi8(h, p);
        uint32_t mask = (uint32_t)_mm_movemask_epi8(c);
        uint32_t need = (uint32_t)((1ull << len) - 1ull);
        return (mask & need) == need;
    }

    if (len <= 32)
    {
        __m256i h = _mm256_loadu_si256((const __m256i *)hay);
        __m256i p = _mm256_loadu_si256((const __m256i *)pat);
        __m256i c = _mm256_cmpeq_epi8(h, p);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(c);
        uint32_t need = (uint32_t)((1ull << len) - 1ull);
        return (mask & need) == need;
    }

    // 33..64: check first 32 and then tail (1..32)
    __m256i h0 = _mm256_loadu_si256((const __m256i *)hay);
    __m256i p0 = _mm256_loadu_si256((const __m256i *)pat);
    __m256i c0 = _mm256_cmpeq_epi8(h0, p0);
    uint32_t mask0 = (uint32_t)_mm256_movemask_epi8(c0);
    if (mask0 != 0xFFFFFFFFu)
        return false;

    size_t tail = len - 32;
    __m256i h1 = _mm256_loadu_si256((const __m256i *)(hay + 32));
    __m256i p1 = _mm256_loadu_si256((const __m256i *)(pat + 32));
    __m256i c1 = _mm256_cmpeq_epi8(h1, p1);
    uint32_t mask1 = (uint32_t)_mm256_movemask_epi8(c1);
    uint32_t need1 = (uint32_t)((1ull << tail) - 1ull);
    return (mask1 & need1) == need1;
}

// ----------------------------- SIMD full compare for arbitrary length -----------------------------

// Compare hay[0..len-1] == pat[0..len-1] using AVX2 blocks for large len.
// This function returns true only if full match. It requires that hay and pat
// have at least len bytes accessible.
static inline bool simd_compare_any(const uint8_t *hay, const uint8_t *pat, size_t len)
{
    // Fast path for small sizes reusing previous function
    if (len <= 64)
        return simd_compare_1_64(hay, pat, len);

    // For len > 64, compare 32-byte blocks with AVX2
    size_t off = 0;
    // compare 32-byte blocks while we have >=32 bytes remaining
    while (len - off >= 32)
    {
        __m256i h = _mm256_loadu_si256((const __m256i *)(hay + off));
        __m256i p = _mm256_loadu_si256((const __m256i *)(pat + off));
        __m256i c = _mm256_cmpeq_epi8(h, p);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(c);
        if (mask != 0xFFFFFFFFu)
            return false;
        off += 32;
    }
    // remaining tail (1..31)
    size_t tail = len - off;
    if (tail == 0)
        return true;
    // use simd_compare_1_64 for the final tail (safe up to 64)
    return simd_compare_1_64(hay + off, pat + off, tail);
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