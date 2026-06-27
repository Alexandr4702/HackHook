#include "MemoryCache.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <vector>

void MemoryCache::put(std::vector<uint8_t> data, View &&v)
{
    if (v.range.size == 0)
        return;

    const RegionRange target = v.range;

    m_views.insert(std::move(v));
    add_region(target);

    const size_t copy_size = std::min(data.size(), target.size);
    data.resize(copy_size);

    replace_data(target, std::move(data));
}

bool MemoryCache::update_data(const RegionRange &range, std::vector<uint8_t> data)
{
    if (range.size == 0 || data.size() != range.size)
        return false;

    return replace_data(range, std::move(data));
}

std::optional<std::vector<uint8_t>> MemoryCache::data(const RegionRange &range) const
{
    if (range.size == 0)
        return std::vector<uint8_t>{};

    auto it = find_containing_region(range);
    if (it == m_regions.end())
        return std::nullopt;

    const size_t offset = static_cast<size_t>(range.base - it->range.base);
    return std::vector<uint8_t>(it->data.begin() + offset, it->data.begin() + offset + range.size);
}

std::set<MemoryCache::Region>::iterator MemoryCache::find_containing_region(const RegionRange &range)
{
    Region lookup{.range = range};
    auto it = m_regions.upper_bound(lookup);
    if (it != m_regions.begin())
        --it;

    if (it == m_regions.end() || !it->range.contains(range))
        return m_regions.end();

    return it;
}

std::set<MemoryCache::Region>::const_iterator MemoryCache::find_containing_region(const RegionRange &range) const
{
    Region lookup{.range = range};
    auto it = m_regions.upper_bound(lookup);
    if (it != m_regions.begin())
        --it;

    if (it == m_regions.end() || !it->range.contains(range))
        return m_regions.end();

    return it;
}

bool MemoryCache::replace_data(const RegionRange &range, std::vector<uint8_t> data)
{
    if (range.size == 0 || data.empty())
        return false;

    auto it = find_containing_region(range);
    if (it == m_regions.end())
        return false;

    Region updated = std::move(m_regions.extract(it).value());
    const size_t offset = static_cast<size_t>(range.base - updated.range.base);
    std::copy(data.begin(), data.end(), updated.data.begin() + offset);
    m_regions.insert(std::move(updated));
    return true;
}

void MemoryCache::remove_view(const View &el)
{
    auto it = m_views.find(el);
    if (it == m_views.end())
    {
        return;
    }

    m_views.erase(it);

    std::vector<RegionRange> to_remove{el.range};

    for (const auto &view : m_views)
    {
        if (!view.range.overlaps(el.range))
            continue;

        std::vector<RegionRange> next;

        for (const auto &r : to_remove)
        {
            if (!r.overlaps(view.range))
            {
                next.push_back(r);
                continue;
            }

            if (view.range.base > r.base)
            {
                next.push_back({.base = r.base, .size = static_cast<size_t>(view.range.base - r.base)});
            }

            if (view.range.end() < r.end())
            {
                next.push_back({.base = view.range.end(), .size = static_cast<size_t>(r.end() - view.range.end())});
            }
        }

        to_remove.swap(next);

        if (to_remove.empty())
            break;
    }

    for (const auto &r : to_remove)
    {
        if (r.size != 0)
            remove_region(r);
    }
}

void MemoryCache::add_region(const RegionRange &region)
{
    if (region.size == 0)
        return;

    if (m_regions.empty())
    {
        m_regions.insert({region, std::vector<uint8_t>(region.size)});
        return;
    }
    Region tmp(region);
    auto it = m_regions.lower_bound(tmp);

    if ((it != m_regions.begin() && std::prev(it)->range.overlapsInclusive(region)))
    {
        it = std::prev(it);
    }

    if (it == m_regions.end())
    {
        m_regions.insert({region, std::vector<uint8_t>(region.size)});
        return;
    }

    if (it->range.contains(region))
    {
        return;
    }

    const uint64_t new_base = std::min(it->range.base, region.base);
    uint64_t new_end = region.end();

    for (auto it_tmp = it; it_tmp != m_regions.end() && region.overlapsInclusive(it_tmp->range); ++it_tmp)
    {
        new_end = std::max(new_end, it_tmp->range.end());
    }
    const size_t new_size = new_end - new_base;
    std::vector<uint8_t> overlapped_data(new_size);

    while (it != m_regions.end() && region.overlapsInclusive(it->range))
    {
        const size_t offset = it->range.base - new_base;
        const size_t copy_size = std::min(it->data.size(), it->range.size);
        if (copy_size != 0)
            std::copy_n(it->data.begin(), copy_size, overlapped_data.begin() + offset);
        it = m_regions.erase(it);
    }
    m_regions.insert(Region{.range = {.base = new_base, .size = new_size}, .data = std::move(overlapped_data)});
}

void MemoryCache::remove_region(const RegionRange &region)
{
    if (m_regions.empty())
        return;

    Region tmp = {.range = region};
    auto it_begin = m_regions.lower_bound(tmp);
    auto it_end = it_begin;

    if ((it_begin != m_regions.begin() && std::prev(it_begin)->range.overlaps(region)))
    {
        it_begin = std::prev(it_begin);
        it_end = it_begin;
    }

    if (it_begin == m_regions.end() || !it_begin->range.overlaps(region))
    {
        return;
    }

    while (std::next(it_end) != m_regions.end() && region.overlaps(std::next(it_end)->range))
    {
        ++it_end;
    }

    std::array<uint64_t, 2> first_region = {it_begin->range.base, region.base},
                            last_region = {region.end(), it_end->range.end()};

    RegionRange begin_reg = {.base = first_region[0],
                             .size = first_region[1] > first_region[0] ? first_region[1] - first_region[0] : 0};

    RegionRange end_reg = {.base = last_region[0],
                           .size = last_region[1] > last_region[0] ? last_region[1] - last_region[0] : 0};

    std::vector<uint8_t> first_data;
    std::vector<uint8_t> last_data;

    if (begin_reg.size != 0)
    {
        first_data.assign(it_begin->data.begin(), it_begin->data.begin() + begin_reg.size);
    }

    if (end_reg.size != 0)
    {
        const size_t offset = region.end() - it_end->range.base;

        last_data.assign(it_end->data.begin() + offset, it_end->data.end());
    }

    m_regions.erase(it_begin, std::next(it_end));

    if (begin_reg.size != 0)
    {
        m_regions.insert({begin_reg, std::move(first_data)});
    }

    if (end_reg.size != 0)
    {
        m_regions.insert({end_reg, std::move(last_data)});
    }
}
