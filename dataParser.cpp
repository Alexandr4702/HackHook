#include <tbb/parallel_pipeline.h>
#include <tbb/concurrent_vector.h>
#include <tbb/task_arena.h>
#include <fstream>
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <algorithm>
#include <iomanip>

class ParallelCSVParser
{
public:
    struct MemoryRegion
    {
        uint64_t BaseAddress;
        uint64_t RegionSize;
        uint64_t DumpOffset;
        uint64_t DumpSize;
        std::string State;
        std::string Protect;
        std::string Type;
        std::string ModuleName;
        std::string SectionName;
        bool operator==(const MemoryRegion &other) const
        {
            return BaseAddress == other.BaseAddress && RegionSize == other.RegionSize && DumpOffset == other.DumpOffset && DumpSize == other.DumpSize && State == other.State && Protect == other.Protect && Type == other.Type && ModuleName == other.ModuleName && SectionName == other.SectionName;
        }
    };

    static std::vector<MemoryRegion> ParseFile(const std::string &filename)
    {
        tbb::concurrent_vector<MemoryRegion> result;
        std::ifstream file(filename);

        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filename);
        }

        std::string line;
        std::getline(file, line);

        tbb::parallel_pipeline(
            std::thread::hardware_concurrency(),

            tbb::make_filter<void, std::string>(
                tbb::filter_mode::serial_in_order,
                [&](tbb::flow_control &fc) -> std::string
                {
                    if (!std::getline(file, line))
                    {
                        fc.stop();
                        return "";
                    }
                    return line;
                }) &

                tbb::make_filter<std::string, MemoryRegion>(
                    tbb::filter_mode::parallel,
                    [](const std::string &line)
                    {
                        return ParseLine(line);
                    }) &

                tbb::make_filter<MemoryRegion, void>(
                    tbb::filter_mode::serial_in_order,
                    [&](const MemoryRegion &region)
                    {
                        result.push_back(region);
                    }));

        file.close();
        return {result.begin(), result.end()};
    }

private:
    static MemoryRegion ParseLine(const std::string &line)
    {
        MemoryRegion region;
        size_t start = 0;
        size_t end = line.find(',');

        auto parseHex = [](const std::string &s)
        {
            return std::stoull(s, nullptr, 16);
        };

        auto parseUll = [](const std::string &s)
        {
            return std::stoull(s, nullptr, 10);
        };

        // BaseAddress
        if (end != std::string::npos)
        {
            region.BaseAddress = parseHex(line.substr(start, end - start));
            start = end + 1;
            end = line.find(',', start);
        }

        // RegionSize
        if (end != std::string::npos)
        {
            region.RegionSize = parseUll(line.substr(start, end - start));
            start = end + 1;
            end = line.find(',', start);
        }

        // DumpOffset
        if (end != std::string::npos)
        {
            region.DumpOffset = parseHex(line.substr(start, end - start));
            start = end + 1;
            end = line.find(',', start);
        }

        // DumpSize
        if (end != std::string::npos)
        {
            region.DumpSize = parseUll(line.substr(start, end - start));
            start = end + 1;
            end = line.find(',', start);
        }

        // State
        if (end != std::string::npos)
        {
            region.State = line.substr(start, end - start);
            start = end + 1;
            end = line.find(',', start);
        }

        // Protect
        if (end != std::string::npos)
        {
            region.Protect = line.substr(start, end - start);
            start = end + 1;
            end = line.find(',', start);
        }

        // Type
        if (end != std::string::npos)
        {
            region.Type = line.substr(start, end - start);
            start = end + 1;
            end = line.find(',', start);
        }

        // ModuleName
        if (end != std::string::npos)
        {
            region.ModuleName = ParseField(line.substr(start, end - start));
            start = end + 1;
            end = line.find(',', start);
        }

        // SectionName
        region.SectionName = ParseField(line.substr(start));

        return region;
    }

    static std::string ParseField(std::string field)
    {

        field.erase(0, field.find_first_not_of(" \t"));
        field.erase(field.find_last_not_of(" \t") + 1);

        if (field.size() >= 2 && field.front() == '"' && field.back() == '"')
        {
            field = field.substr(1, field.size() - 2);

            size_t pos = 0;
            while ((pos = field.find("\"\"", pos)) != std::string::npos)
            {
                field.replace(pos, 2, "\"");
                pos += 1;
            }
        }
        return field;
    }
};

std::ostream &operator<<(std::ostream &os, const std::vector<char> &data)
{
    for (auto byte : data)
    {
        os << std::format("{:02x} ", byte);
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, const std::span<char> data)
{
    for (auto byte : data)
    {
        os << std::format("{:02x} ", byte);
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, const ParallelCSVParser::MemoryRegion &data)
{
    std::cout << std::format("{:016x} {:016x} {:016x} {:016x} {:s} {:s} {:s} {:s} {:s}", data.BaseAddress, data.RegionSize, data.DumpOffset, data.DumpSize, data.State, data.Protect, data.Type, data.ModuleName, data.SectionName);
    return os;
}

std::u16string ascii2utf16(std::string_view ascii_str)
{
    std::u16string utf16_str;
    utf16_str.reserve(ascii_str.size());

    for (unsigned char c : ascii_str)
    {
        if (c <= 127)
        {
            utf16_str.push_back(static_cast<char16_t>(c));
        }
        else
        {
            utf16_str.push_back(u'?');
        }
    }

    return utf16_str;
}

uint64_t dumpOffset2VirtualAddress(const std::vector<ParallelCSVParser::MemoryRegion> &regions, uint64_t dumpOffset)
{
    ParallelCSVParser::MemoryRegion reg = {.DumpOffset = dumpOffset};
    auto region = upper_bound(regions.begin(), regions.end(), reg, [](const auto &a, const auto &b)
                              { return a.DumpOffset < b.DumpOffset; });
    --region;
    if (dumpOffset >= region->DumpOffset && dumpOffset <= region->DumpOffset + region->DumpSize)
    {
        return region->BaseAddress + (dumpOffset - region->DumpOffset);
    }

    return 0;
}

uint64_t virtualAddress2dumpOffset(const std::vector<ParallelCSVParser::MemoryRegion> &regions, uint64_t virtualAddress)
{

    ParallelCSVParser::MemoryRegion reg = {.DumpOffset = virtualAddress};
    auto region = upper_bound(regions.begin(), regions.end(), reg, [](const auto &a, const auto &b)
                              { return a.BaseAddress < b.BaseAddress; });
    --region;
    if (virtualAddress >= region->BaseAddress && virtualAddress <= region->BaseAddress + region->DumpSize)
    {
        return region->DumpOffset + (virtualAddress - region->BaseAddress);
    }

    return 0;
}

int main()
{
    using namespace std;
    try
    {
        auto regions = ParallelCSVParser::ParseFile("./mem_map.csv");
        auto regionsCopy = regions;

        sort(regions.begin(), regions.end(), [](const auto &a, const auto &b)
             { return std::tie(a.DumpOffset, a.BaseAddress) < std::tie(b.DumpOffset, b.BaseAddress); });

        // for (size_t i = 0; i < std::min(size_t(120), regions.size()); ++i)
        // {
        //     const auto &r = regions[i];
        //     std::cout << std::format("0x{:<30x} | {:16} | 0x{:<16x} | {:16} | {:16} | {:16} | {:16}\n",
        //                              r.BaseAddress, r.RegionSize, r.DumpOffset, r.DumpSize,
        //                              r.State, r.Protect, r.Type);
        // }
        // std::cout << "Total regions: " << regions.size() << std::endl;

        ifstream dumpFile("mem_dump.bin", ios::binary);
        if (!dumpFile.is_open())
        {
            std::cerr << "Failed to open dump file\n";
            return 1;
        }
        dumpFile.seekg(0, std::ios::end);
        size_t fileSize = dumpFile.tellg();
        dumpFile.seekg(0, std::ios::beg);
        size_t regionsSize = 0;
        for (const auto &region : regions)
        {
            // break;
            if (region.State == "COMMIT" &&
                region.Protect == "READWRITE" &&
                region.Type == "PRIVATE" &&
                region.ModuleName.empty() &&
                region.SectionName.empty())
            {
                if (region.DumpSize == 0 ||
                    region.DumpOffset + region.DumpSize > fileSize)
                {
                    continue;
                }
                regionsSize += region.DumpSize;
                const size_t readSize = 64;
                size_t bytesToRead = std::min(static_cast<size_t>(readSize), region.DumpSize);

                vector<char> buffer(bytesToRead);
                dumpFile.seekg(region.DumpOffset);
                dumpFile.read(buffer.data(), bytesToRead);

                // cout << format("Region at 0x{:x} (size: {:d} bytes, dump offset: 0x{:x}):\n",
                //                region.BaseAddress, region.RegionSize, region.DumpOffset);
                // cout << buffer << endl;
                // cout << "\n";
            }
        }
        cout << "Total regions size: " << regionsSize / 1024 / 1024 << endl;

        vector<pair<uint64_t, std::string>> offsetsNames = {
            {0x3D5C6158, "Abdurait"},
            {0x6EF44B00, "Abdurait"},
            {0x91AAA800, "Abdurait"},
            {0x91AAA820, "Abdurait"},
            {0x92608B80, "Abdurait"},
            {0x926093E0, "Abdurait"},
            {0x92609E00, "Abdurait"},
            {0xA927C600, "Abdurait"},
            {0x76D3ADD0, "Angron"},
            {0x76D3BBD0, "Angron"},
            {0x76D3C7A0, "Angron"},
            {0x76D43AD0, "Angron"},
            {0x76D44BD0, "Angron"},
            {0x76D44F10, "Angron"},
            {0x7C55B618, "Angron"},
            {0x876C00C0, "Angron"},
            {0x918418B0, "Angron"},
            {0x9184AA50, "Angron"},
            {0xA9139298, "Angron"},
            {0xA9141980, "Angron"},
            {0x76D40CE0, "Arshir"},
            {0x76D47FB0, "Arshir"},
            {0x86732640, "Arshir"},
            {0x867340F0, "Arshir"},
            {0x86734270, "Arshir"},
            {0x86735D10, "Arshir"},
            {0xA927C140, "Arshir"},
            {0xA927C798, "Arshir"},
            {0x3D5D4A18, "Gomboev"},
            {0x40717F80, "Gomboev"},
            {0x40718F20, "Gomboev"},
            {0x76C68D80, "Gomboev"},
            {0x916399E0, "Gomboev"},
            {0x916399F0, "Gomboev"},
            {0x91639DC0, "Gomboev"},
            {0x91639DD0, "Gomboev"},
            {0x86116050, "Jabkoa"},
            {0x86731620, "Jabkoa"},
            {0x867316B0, "Jabkoa"},
            {0x86734AB0, "Jabkoa"},
            {0x86734F40, "Jabkoa"},
            {0x867357C0, "Jabkoa"},
            {0xA927FA80, "Jabkoa"},
            {0xA9280A58, "Jabkoa"},
            {0x8B7F9E00, "Paveleva"},
            {0x8B7F9E80, "Paveleva"},
            {0x91AA8B80, "Paveleva"},
            {0x91AA9160, "Paveleva"},
            {0x91AA9260, "Paveleva"},
            {0x91AA9CA0, "Paveleva"},
            {0xA927A5D8, "Paveleva"},
            {0xA927B480, "Paveleva"},
            {0x9162D340, "Suihita"},
            {0x91637630, "Suihita"},
            {0x91638840, "Suihita"},
            {0x91638E30, "Suihita"},
            {0x963C30E0, "Suihita"},
            {0x963C3100, "Suihita"},
            {0xA9276F40, "Suihita"},
            {0xA927B6D8, "Suihita"}};

        for (auto [offset, name] : offsetsNames)
        {
            ParallelCSVParser::MemoryRegion reg = {.DumpOffset = offset};
            auto region = upper_bound(regions.begin(), regions.end(), reg, [](const auto &a, const auto &b)
                                      { return a.DumpOffset < b.DumpOffset; });
            --region;
            if (offset >= region->DumpOffset && offset <= region->DumpOffset + region->DumpSize)
            {
                span<char> nameBytes = {reinterpret_cast<char *>(ascii2utf16(name).data()), name.size() * 2};
                cout << format("0x{:X} (Base: 0x{:X}, Size: {:d}, Dump Offset: 0x{:X}, Iternal offset: {}) - {} ",
                               offset, region->BaseAddress, region->RegionSize, region->DumpOffset, offset - region->DumpOffset, name);
                cout << nameBytes << endl;
                const size_t readSize = 64;
                size_t bytesToRead = min(static_cast<size_t>(readSize), region->DumpSize);
                vector<char> buffer(bytesToRead);
                dumpFile.seekg(region->DumpOffset);
                dumpFile.read(buffer.data(), bytesToRead);
                cout << buffer << endl;

                const size_t readSizeName = 128;
                buffer.resize(readSizeName);
                size_t readOffset = max(region->DumpOffset, offset - 64);
                dumpFile.seekg(readOffset);
                dumpFile.read(buffer.data(), readSizeName);
                cout << buffer << endl;
            }
            else
            {
                std::cout << format("0x{:X} not found {}", offset, name) << std::endl;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}