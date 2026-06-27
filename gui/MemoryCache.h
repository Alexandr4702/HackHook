#ifndef MEMORYCACHE_H
#define MEMORYCACHE_H

#include "interface_generated.h"
#include "myhook/FoundOccurrences.h"
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <stdint.h>
#include <vector>

class MemoryCache
{
  public:
    struct RegionRange
    {
        uint64_t base = 0;
        size_t size = 0;

        auto operator<=>(const RegionRange &) const = default;

        [[nodiscard]]
        uint64_t end() const noexcept
        {
            return base + size;
        }

        [[nodiscard]]
        bool overlaps(const RegionRange &other) const noexcept
        {
            return other.base < end() && base < other.end();
        }

        [[nodiscard]]
        bool overlaps(uint64_t b, uint64_t e) const noexcept
        {
            return overlaps({b, static_cast<size_t>(e - b)});
        }

        [[nodiscard]]
        bool overlapsInclusive(const RegionRange &other) const noexcept
        {
            return !(other.end() < base || other.base > end());
        }

        [[nodiscard]]
        bool contains(const RegionRange &other) const noexcept
        {
            return other.base >= base && other.end() <= end();
        }

        [[nodiscard]]
        bool contains(uint64_t address, size_t sz) const noexcept
        {
            return contains({address, sz});
        }
    };

    struct Region
    {
        RegionRange range;
        std::vector<uint8_t> data;
        auto operator<=>(const Region &other) const
        {
            return range <=> other.range;
        }
        bool operator==(const Region &other) const
        {
            return range == other.range;
        }
    };

    struct View
    {
        RegionRange range;
        FoundOccurrences occurrence;

        View(RegionRange range, Interface::ValueType type)
            : range(range), occurrence{range.base, 0, range.size, range.size, static_cast<int32_t>(type)}
        {
        }

        explicit View(FoundOccurrences occurrence)
            : range{occurrence.baseAddress + occurrence.offset, static_cast<size_t>(occurrence.data_size)},
              occurrence(occurrence)
        {
        }

        [[nodiscard]]
        Interface::ValueType type() const noexcept
        {
            return static_cast<Interface::ValueType>(occurrence.type);
        }

        std::strong_ordering operator<=>(const View &other) const
        {
            if (auto cmp = range <=> other.range; cmp != 0)
                return cmp;

            return occurrence.type <=> other.occurrence.type;
        }
        bool operator==(const View &other) const
        {
            return range == other.range && occurrence.type == other.occurrence.type;
        }
    };

    void add_view(uint64_t address, size_t size, Interface::ValueType type)
    {
        add_view(View{{.base = address, .size = size}, type});
    }

    void add_view(View view)
    {
        if (view.range.size == 0)
            return;

        m_views.insert(view);
        add_region(view.range);
    }

    void remove_view(const View &el);
    void remove_view(uint64_t address, size_t size, Interface::ValueType type)
    {
        View el{{.base = address, .size = size}, type};
        remove_view(el);
    }
    const auto &views() const
    {
        return m_views;
    }

    const auto &regions() const
    {
        return m_regions;
    }

    void clear()
    {
        m_regions.clear();
        m_views.clear();
    }

    void put(std::vector<uint8_t> data, View &&v);
    bool update_data(const RegionRange &range, std::vector<uint8_t> data);
    std::optional<std::vector<uint8_t>> data(const RegionRange &range) const;

  private:
    std::set<Region>::iterator find_containing_region(const RegionRange &range);
    std::set<Region>::const_iterator find_containing_region(const RegionRange &range) const;
    bool replace_data(const RegionRange &range, std::vector<uint8_t> data);
    void add_region(const RegionRange &region);
    void remove_region(const RegionRange &region);

  private:
    std::set<Region> m_regions;
    std::set<View> m_views;
};

#endif
