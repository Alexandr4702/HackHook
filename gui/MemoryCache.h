#include "interface_generated.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdint.h>
#include <vector>


class MemoryCache
{
  public:
    struct RegionRange
    {
        uint64_t base = 0;
        size_t size = 0;
        std::strong_ordering operator<=>(const RegionRange &other) const
        {
            if (auto cmp = base <=> other.base; cmp != 0)
                return cmp;

            return size <=> other.size;
        }

        uint64_t end() const
        {
            return base + size;
        }

        bool overlaps(uint64_t b, uint64_t e) const
        {
            return !(e <= base || b >= end());
        }

        bool overlaps(const RegionRange& other) const
        {
            return !(other.end() <= base || other.base >= end());
        }

        bool contains(uint64_t address, size_t size) const
        {
            return address >= base && (address + size) <= end();
        }

        bool contains(const RegionRange& other) const
        {
            return other.base >= base && other.end() <= end();
        }
    };

    struct Region
    {
        RegionRange range;
        std::vector<uint8_t> data;
        auto operator<=>(const Region& other) const
        {
            return range <=> other.range;
        }
    };

    struct View
    {
        RegionRange range;
        Interface::ValueType type{};
        std::strong_ordering operator<=>(const View &other) const
        {
            if (auto cmp = range <=> other.range; cmp != 0)
                return cmp;

            return type <=> other.type;
        }
    };

    void add_view(uint64_t address, size_t size, Interface::ValueType type)
    {
        m_views.emplace(View{
            .range = {.base = address, .size = size},
            .type = type,
        });

    }

    void add_view(View view)
    {
        m_views.insert(view);
        add_region(view.range);
    }

    void remove_view(uint64_t address, size_t size, Interface::ValueType type)
    {
        View el = View{.range = {.base = address, .size = size}, .type = type};
        auto it = m_views.find(el);
        if (it == m_views.end())
        {
            return;
        }
        m_views.erase(it);

        if (std::any_of(m_views.begin(), m_views.end(),
                        [&el](const View &other) { return other.range.contains(el.range); }))
            return;

        remove_region(el.range);
    }

    // void filter_views(std::span<const uint8_t> pattern)
    // {
    //     std::erase_if(m_views, [&](const View &v) {
    //         auto bytes = read_cached(v.address, v.size);

    //         if (bytes.size() < pattern.size())
    //             return true;

    //         return !std::equal(pattern.begin(), pattern.end(), bytes.begin());
    //     });
    // }

    const auto &views() const
    {
        return m_views;
    }

    const auto &regions() const
    {
        return m_regions;
    }

  private:
    void add_region(const RegionRange &region)
    {
        if(m_regions.empty())
        {
            m_regions.insert({.range = region});
            return;
        }
        Region tmp = {.range = region};
        auto it = std::lower_bound(m_regions.begin(), m_regions.end(), tmp);
        
        if(it != m_regions.begin() && std::prev(it)->range.overlaps(region))
        {
            it = std::prev(it);
        }

        std::vector<RegionRange> overlapped_regions;
        std::vector<uint8_t> overlapped_data;
        uint64_t new_begin = it->range.base;
        uint64_t new_end = it->range.end();
        while(it != m_regions.end() && region.overlaps(it->range))
        {
            new_end = max(new_end, it->range.end());
            size_t current_size = new_end - new_begin;
            overlapped_data.resize(new_end - new_begin);
            std::copy(it->data.begin(), it->data.end(), overlapped_data.begin() + current_size);
            it++;
        }

    }

    void remove_region(const RegionRange& region)
    {
    }

    // std::span<const uint8_t>
    // read_region(uint64_t address,
    //             size_t size) const
    // {
    //     auto it = find_region(address,
    //                           size);

    //     if (it == m_regions.end())
    //         return {};

    //     const auto offset =
    //         address - it->base;

    //     return std::span<const uint8_t>(
    //         it->bytes.data() + offset,
    //         size);
    // }

    // auto find_region(uint64_t address,
    //                  size_t size) const
    // {
    //     return std::ranges::find_if(
    //         m_regions,
    //         [&](const Region& r)
    //         {
    //             return r.contains(address,
    //                               size);
    //         });
    // }

  private:
    std::set<Region> m_regions;
    std::set<View> m_views;
};