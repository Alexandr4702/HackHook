#ifndef BOYERMOOREHORSPOOL_H
#define BOYERMOOREHORSPOOL_H

#include <vector>
#include <cstdint>
#include <span>
#include <functional>
#include <memory_resource>
#include <processthreadsapi.h>

#include "MyHookImport.h"

struct FoundOccurrences
{
    uint64_t baseAddress;
    uint64_t offset;
    uint64_t region_size;
    uint64_t data_size;
    int32_t type;
    auto operator<=>(const FoundOccurrences& b) const noexcept
    {
        return (baseAddress + offset) <=> (b.baseAddress + b.offset);
    }
};

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
    class ProtectGuard
    {
        void *addr;
        size_t size;
        DWORD old;

      public:
        ProtectGuard(void *a, size_t s) : addr(a), size(s)
        {
            VirtualProtect(addr, size, PAGE_EXECUTE_READWRITE, &old);
        }

        ~ProtectGuard()
        {
            DWORD tmp;
            VirtualProtect(addr, size, old, &tmp);
        }
    };
    PVOID m_veHandle = nullptr;
};

#endif // BOYERMOOREHORSPOOL_H
