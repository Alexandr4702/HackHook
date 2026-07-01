#include "MemDumper.h"
#include "MemoryScanner.h"
#include "MyHook.h"

#include <algorithm>
#include <fstream>
#include <sstream>

std::vector<THREAD_BASIC_INFORMATION> GetAllThreadInfo(DWORD processId)
{
    std::vector<THREAD_BASIC_INFORMATION> threadInfos;

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE)
        return threadInfos;

    THREADENTRY32 te;
    te.dwSize = sizeof(te);

    if (!Thread32First(hSnap, &te))
    {
        CloseHandle(hSnap);
        return threadInfos;
    }

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll)
    {
        CloseHandle(hSnap);
        return threadInfos;
    }

    auto NtQueryInformationThread = (pNtQueryInformationThread)GetProcAddress(hNtdll, "NtQueryInformationThread");

    if (!NtQueryInformationThread)
    {
        CloseHandle(hSnap);
        return threadInfos;
    }

    do
    {
        if (te.th32OwnerProcessID == processId)
        {
            HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
            if (hThread)
            {
                THREAD_BASIC_INFORMATION tbi;
                NTSTATUS status = NtQueryInformationThread(hThread, ThreadBasicInformation, &tbi, sizeof(tbi), nullptr);
                if (status == 0) // STATUS_SUCCESS
                    threadInfos.push_back(tbi);

                CloseHandle(hThread);
            }
        }
    } while (Thread32Next(hSnap, &te));

    CloseHandle(hSnap);
    return threadInfos;
}

std::unordered_map<LPVOID, ModuleInfo> BuildModuleMap(HANDLE hProcess, MemTool &mem_tool)
{
    std::unordered_map<LPVOID, ModuleInfo> moduleMap;

    std::vector<HMODULE> modules(256);
    DWORD cbNeeded = 0;
    while (true)
    {
        const DWORD bufferSize = static_cast<DWORD>(modules.size() * sizeof(HMODULE));
        if (!EnumProcessModulesEx(hProcess, modules.data(), bufferSize, &cbNeeded, LIST_MODULES_ALL))
            return moduleMap;

        if (cbNeeded <= bufferSize)
            break;

        modules.resize((cbNeeded + sizeof(HMODULE) - 1) / sizeof(HMODULE));
    }
    modules.resize(cbNeeded / sizeof(HMODULE));

    for (HMODULE module : modules)
    {
        char moduleName[MAX_PATH] = {};
        LPVOID baseAddress = module;

        if (!GetModuleFileNameExA(hProcess, module, moduleName, MAX_PATH))
            continue;

        IMAGE_DOS_HEADER dosHeader;
        if (mem_tool.read(reinterpret_cast<uintptr_t>(baseAddress), &dosHeader, sizeof(dosHeader)) !=
                sizeof(dosHeader) ||
            dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
            continue;

        IMAGE_NT_HEADERS ntHeaders;
        LPBYTE ntHeaderAddr = static_cast<LPBYTE>(baseAddress) + dosHeader.e_lfanew;
        if (mem_tool.read(reinterpret_cast<uintptr_t>(ntHeaderAddr), &ntHeaders, sizeof(ntHeaders)) !=
                sizeof(ntHeaders) ||
            ntHeaders.Signature != IMAGE_NT_SIGNATURE)
            continue;

        std::vector<SectionRange> sectionRanges;
        LPBYTE sectionHeaderAddr = ntHeaderAddr + sizeof(IMAGE_NT_HEADERS);

        for (int j = 0; j < ntHeaders.FileHeader.NumberOfSections; ++j)
        {
            IMAGE_SECTION_HEADER sectionHeader;
            LPBYTE thisSectionAddr = sectionHeaderAddr + j * sizeof(IMAGE_SECTION_HEADER);
            if (mem_tool.read(reinterpret_cast<uintptr_t>(thisSectionAddr), &sectionHeader, sizeof(sectionHeader)) !=
                sizeof(sectionHeader))
                continue;

            uintptr_t sectionStart = reinterpret_cast<uintptr_t>(baseAddress) + sectionHeader.VirtualAddress;
            uintptr_t sectionEnd = sectionStart + sectionHeader.Misc.VirtualSize;

            std::string name(reinterpret_cast<char *>(sectionHeader.Name), 8);
            name.erase(name.find_last_not_of('\0') + 1);

            sectionRanges.push_back({sectionStart, sectionEnd, name});
        }

        moduleMap[baseAddress] = {moduleName, baseAddress, std::move(sectionRanges)};
    }

    return moduleMap;
}

std::vector<SectionInfo> dumpWithMapping(const std::string &filePath, HANDLE hProcess, MemTool &mem_tool)
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    LPVOID currentAddress = sysInfo.lpMinimumApplicationAddress;
    LPVOID maxAddress = sysInfo.lpMaximumApplicationAddress;

    std::unordered_map<LPVOID, ModuleInfo> moduleMap = BuildModuleMap(hProcess, mem_tool);
    std::vector<SectionInfo> sections;
    size_t currentDumpOffset = 0;

    constexpr size_t buffer_size = 1024 * 1024;
    std::vector<uint8_t> buffer(buffer_size);

    std::ofstream outFile(filePath, std::ios::binary);
    if (!outFile.is_open())
    {
        // g_log << "Failed to open file for writing\n";
        return sections;
    }

    while (currentAddress < maxAddress)
    {
        MEMORY_BASIC_INFORMATION mbi;
        size_t dumpSize = 0;
        if (VirtualQuery(currentAddress, &mbi, sizeof(mbi)) == 0)
            break;

        std::string moduleName = "";
        std::string sectionName = "";

        auto it = moduleMap.find(mbi.AllocationBase);
        if (it != moduleMap.end())
        {
            moduleName = it->second.moduleName;
            uintptr_t addr = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            for (const auto &s : it->second.sectionRanges)
            {
                if (addr >= s.start && addr < s.end)
                {
                    sectionName = s.name;
                    break;
                }
            }
        }

        if ((mbi.State == MEM_COMMIT) && mbi.Protect != PAGE_NOACCESS && !(mbi.Protect & PAGE_GUARD))
        {
            const auto region_base = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            while (dumpSize < mbi.RegionSize)
            {
                const size_t chunk_size = std::min(buffer.size(), mbi.RegionSize - dumpSize);
                const size_t bytes_read = mem_tool.read(region_base + dumpSize, buffer.data(), chunk_size);
                if (bytes_read == 0)
                    break;

                outFile.write(reinterpret_cast<const char *>(buffer.data()), bytes_read);
                dumpSize += bytes_read;

                if (!outFile || bytes_read != chunk_size)
                    break;
            }
        }

        currentAddress = reinterpret_cast<uint8_t *>(currentAddress) + mbi.RegionSize;
        sections.emplace_back(SectionInfo(mbi.BaseAddress, mbi.RegionSize, currentDumpOffset, dumpSize, mbi.State,
                                          mbi.Protect, mbi.Type, moduleName, sectionName));
        currentDumpOffset += dumpSize;
    }

    outFile.close();
    return sections;
}

void DumpMemoryMapToCSV(const std::string &outputCsv, const std::vector<SectionInfo> &sections)
{
    std::ofstream outFile(outputCsv);
    if (!outFile.is_open())
    {
        // g_log << "[DumpMemoryMapToCSV] Failed to open file for writing\n";
        return;
    }
    outFile << "BaseAddress,RegionSize,DumpOffset,DumpSize,State,Protect,Type,ModuleName,SectionName\n";

    for (const auto &section : sections)
    {
        std::string state, protect, type;

        switch (section.state)
        {
        case MEM_COMMIT:
            state = "COMMIT";
            break;
        case MEM_RESERVE:
            state = "RESERVE";
            break;
        case MEM_FREE:
            state = "FREE";
            break;
        default:
            state = "UNKNOWN";
            break;
        }

        switch (section.protect & 0xFF)
        {
        case PAGE_NOACCESS:
            protect = "NOACCESS";
            break;
        case PAGE_READONLY:
            protect = "READONLY";
            break;
        case PAGE_READWRITE:
            protect = "READWRITE";
            break;
        case PAGE_WRITECOPY:
            protect = "WRITECOPY";
            break;
        case PAGE_EXECUTE:
            protect = "EXECUTE";
            break;
        case PAGE_EXECUTE_READ:
            protect = "EXECUTE_READ";
            break;
        case PAGE_EXECUTE_READWRITE:
            protect = "EXECUTE_READWRITE";
            break;
        case PAGE_EXECUTE_WRITECOPY:
            protect = "EXECUTE_WRITECOPY";
            break;
        default:
            protect = "UNKNOWN";
            break;
        }

        if (section.protect & PAGE_GUARD)
            protect += "|GUARD";
        if (section.protect & PAGE_NOCACHE)
            protect += "|NOCACHE";
        if (section.protect & PAGE_WRITECOMBINE)
            protect += "|WRITECOMBINE";

        switch (section.type)
        {
        case MEM_IMAGE:
            type = "IMAGE";
            break;
        case MEM_MAPPED:
            type = "MAPPED";
            break;
        case MEM_PRIVATE:
            type = "PRIVATE";
            break;
        case MEM_64K_PAGES:
            type = "MEM_64K_PAGES";
            break;
        case MEM_4MB_PAGES:
            type = "MEM_4MB_PAGES";
            break;
        default:
            type = "UNKNOWN_" + std::to_string(section.type);
            break;
        }

        std::stringstream ss;
        ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(section.baseAddress) << "," << std::dec
           << section.regionSize << ","
           << "0x" << std::hex << section.dumpOffset << "," << std::dec << section.dumpSize << "," << state << ","
           << protect << "," << type << ","
           << "\"" << section.moduleName << "\","
           << "\"" << section.sectionName << "\"\n";

        outFile << ss.str();
    }

    outFile.close();
}

class HeapAnalyzer
{
  public:
    explicit HeapAnalyzer(std::ostream &output) : out(output)
    {
    }

    bool Analyze(bool csvFormat = false)
    {
        DWORD numberOfHeaps;
        std::vector<HANDLE> heaps;

        numberOfHeaps = ::GetProcessHeaps(0, nullptr);
        heaps.resize(numberOfHeaps);

        if (!::GetProcessHeaps(numberOfHeaps, heaps.data()))
        {
            out << "Error: Failed to get process heaps (Error " << GetLastError() << ")\n";
            return false;
        }

        if (csvFormat)
        {
            out << "HeapID;BlockAddress;Size;Type;RegionStart;CommittedSize;UncommittedSize\n";
        }
        else
        {
            out << "=== Process heaps analysis ===\n";
            out << "Total heaps: " << numberOfHeaps << "\n\n";
        }

        for (size_t i = 0; i < heaps.size(); ++i)
        {
            if (!csvFormat)
            {
                out << "Heap #" << i << " at " << heaps[i] << ":\n";
            }
            if (!DumpHeapInfo(heaps[i], i, csvFormat))
            {
                out << (csvFormat ? "" : "  [ERROR: Heap walk failed]\n");
            }
            if (!csvFormat)
            {
                out << "\n";
            }
        }

        return true;
    }

  private:
    std::ostream &out;

    bool DumpHeapInfo(HANDLE heap, size_t heapId, bool csvFormat)
    {
        PROCESS_HEAP_ENTRY entry = {0};

        while (::HeapWalk(heap, &entry))
        {
            if (csvFormat)
            {
                // CSV: HeapID;BlockAddress;Size;Type;RegionStart;CommittedSize;UncommittedSize
                out << heapId << ";" << entry.lpData << ";" << entry.cbData << ";";

                if (entry.wFlags & PROCESS_HEAP_REGION)
                {
                    out << "REGION;" << entry.Region.lpFirstBlock << ";" << entry.Region.dwCommittedSize << ";"
                        << entry.Region.dwUnCommittedSize << "\n";
                }
                else if (entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)
                {
                    out << "UNCOMMITTED;;;\n";
                }
                else if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
                {
                    out << "ALLOCATED;;;\n";
                }
                else
                {
                    out << "UNKNOWN;;;\n";
                }
            }
            else
            {
                out << "  Block at " << entry.lpData << ", size: " << entry.cbData << " bytes";

                if (entry.wFlags & PROCESS_HEAP_REGION)
                {
                    out << " [REGION]\n";
                    out << "    Start: " << entry.Region.lpFirstBlock << "\n";
                    out << "    Committed: " << entry.Region.dwCommittedSize << " bytes\n";
                    out << "    Uncommitted: " << entry.Region.dwUnCommittedSize << " bytes\n";
                }
                else if (entry.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE)
                {
                    out << " [UNCOMMITTED]\n";
                }
                else if (entry.wFlags & PROCESS_HEAP_ENTRY_BUSY)
                {
                    out << " [ALLOCATED]\n";
                }
                else
                {
                    out << " [UNKNOWN]\n";
                }
            }
        }

        const DWORD err = ::GetLastError();
        return (err == ERROR_NO_MORE_ITEMS);
    }
};

void MemRead(std::string out_location, MemTool &mem_tool)
{
    auto threadInfos = GetAllThreadInfo(GetCurrentProcessId());
    std::ofstream outFile(out_location + "threads_dump.csv");
    outFile << "ExitStatus,TebBaseAddress,UniqueProcess,UniqueThread,AffinityMask,Priority,BasePriority\n";
    for (auto threadInfo : threadInfos)
    {
        outFile << threadInfo.ExitStatus << "," << threadInfo.TebBaseAddress << "," << threadInfo.ClientId.UniqueProcess
                << "," << threadInfo.ClientId.UniqueThread << "," << threadInfo.AffinityMask << ","
                << threadInfo.Priority << "," << threadInfo.BasePriority << "\n";
    }

    LOG(MyHook::getInstance().m_log, "MemReadThread started");
    auto sections = dumpWithMapping(out_location + "mem_dump.bin", GetCurrentProcess(), mem_tool);
    DumpMemoryMapToCSV(out_location + "mem_map.csv", sections);
    LOG(MyHook::getInstance().m_log, "DumpMemoryMapToCSV finished");
    std::ofstream csvFile(out_location + "heap_analysis.csv");

    if (csvFile)
    {
        HeapAnalyzer analyzer(csvFile);
        analyzer.Analyze();
        csvFile.close();

        LOG(MyHook::getInstance().m_log, "Heap data saved to heap_analysis.csv");
    }
    else
    {
        LOG(MyHook::getInstance().m_log, "Error: Cannot open CSV file!");
    }

    LOG(MyHook::getInstance().m_log, "MemReadThread finished");
    return;
}

// DWORD MyHook::KeyPressThread()
// {
//     return 0;
//     m_log << "[KeyPressThread] started\n";

//     m_targetHwnd = FindWindowW(nullptr, WINDOW_TITLE.c_str());
//     if (m_targetHwnd)
//     {
//         m_log << "[KeyPressThread] KeyPressThread launched\n";
//     }
//     else
//     {
//         m_log << "[KeyPressThread] Failed to find target window\n";
//         return 1;
//     }

//     while (m_running.load(std::memory_order_acquire))
//     {
//         SendKeyToWindow(m_targetHwnd, g_params.hotkey);
//         // Sleep(100);
//         // SendKeyToWindow(g_targetHwnd, VK_RETURN);
//         Sleep(1000);
//     }

//     m_log << "[KeyPressThread] finished\n";
//     return 0;
// }
