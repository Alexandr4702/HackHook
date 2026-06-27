#ifndef BOYERMOOREHORSPOOL_H
#define BOYERMOOREHORSPOOL_H

#include <vector>
#include <cstdint>
#include <span>
#include <functional>
#include <memory_resource>
#include <processthreadsapi.h>

#include "FoundOccurrences.h"
#include "MyHookImport.h"

struct Region
{
    uint8_t *start;
    uint8_t *end;
    auto operator<=>(const Region &b) const noexcept
    {
        return start == b.start ? end <=> b.end : start <=> b.start;
    }

    bool crosses(const Region &b) const noexcept
    {
        return !(end <= b.start || start >= b.end);
    }
};

class MemTool
{
  public:
    MemTool();
    ~MemTool();
    static MemTool& instance()
    {
        static MemTool instance;
        return instance;
    }
    MemTool(const MemTool &) = delete;
    MemTool &operator=(const MemTool &) = delete;
    MemTool(MemTool &&) = delete;
    MemTool &operator=(MemTool &&) = delete;

    size_t read(uintptr_t address, void* out, size_t size);
    bool write(uintptr_t address, const void *data, size_t size);
    std::pmr::vector<FoundOccurrences> find(std::span<const uint8_t> pattern,
                                            std::pmr::synchronized_pool_resource &pool,
                                            std::pmr::vector<Region> &&exludeReg);

  private:
    PVOID m_veHandle = nullptr;
};

#endif // BOYERMOOREHORSPOOL_H
