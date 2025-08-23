#include "MemDumper.h"

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

std::unordered_map<LPVOID, ModuleInfo> BuildModuleMap(HANDLE hProcess)
{
    std::unordered_map<LPVOID, ModuleInfo> moduleMap;

    HMODULE modules[1024];
    DWORD cbNeeded;
    if (!EnumProcessModulesEx(hProcess, modules, sizeof(modules), &cbNeeded, LIST_MODULES_ALL))
        return moduleMap;

    size_t moduleCount = cbNeeded / sizeof(HMODULE);

    for (size_t i = 0; i < moduleCount; ++i)
    {
        char moduleName[MAX_PATH] = {};
        LPVOID baseAddress = modules[i];

        if (!GetModuleFileNameExA(hProcess, modules[i], moduleName, MAX_PATH))
            continue;

        IMAGE_DOS_HEADER dosHeader;
        if (!ReadProcessMemory(hProcess, baseAddress, &dosHeader, sizeof(dosHeader), nullptr) ||
            dosHeader.e_magic != IMAGE_DOS_SIGNATURE)
            continue;

        IMAGE_NT_HEADERS ntHeaders;
        LPBYTE ntHeaderAddr = static_cast<LPBYTE>(baseAddress) + dosHeader.e_lfanew;
        if (!ReadProcessMemory(hProcess, ntHeaderAddr, &ntHeaders, sizeof(ntHeaders), nullptr) ||
            ntHeaders.Signature != IMAGE_NT_SIGNATURE)
            continue;

        std::vector<SectionRange> sectionRanges;
        LPBYTE sectionHeaderAddr = ntHeaderAddr + sizeof(IMAGE_NT_HEADERS);

        for (int j = 0; j < ntHeaders.FileHeader.NumberOfSections; ++j)
        {
            IMAGE_SECTION_HEADER sectionHeader;
            LPBYTE thisSectionAddr = sectionHeaderAddr + j * sizeof(IMAGE_SECTION_HEADER);
            if (!ReadProcessMemory(hProcess, thisSectionAddr, &sectionHeader, sizeof(sectionHeader), nullptr))
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

std::vector<SectionInfo> dumpWithMapping(const std::string &filePath, HANDLE hProcess)
{
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);

    LPVOID currentAddress = sysInfo.lpMinimumApplicationAddress;
    LPVOID maxAddress = sysInfo.lpMaximumApplicationAddress;

    std::unordered_map<LPVOID, ModuleInfo> moduleMap = BuildModuleMap(hProcess);
    std::vector<SectionInfo> sections;
    size_t currentDumpOffset = 0;

    // const size_t kDefaultBufferSize = 4 * 1024 * 1024;
    // std::vector<BYTE> buffer(kDefaultBufferSize);

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
        if (VirtualQuery( currentAddress, &mbi, sizeof(mbi)) == 0)
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
            outFile.write(reinterpret_cast<const char *>(mbi.BaseAddress), mbi.RegionSize);
            dumpSize = mbi.RegionSize;
            // SIZE_T bytesRead = 0;
            // dumpSize = 0;
            // buffer.reserve(mbi.RegionSize);
            // dumpSize = bytesRead;

            // WINBOOL result = ReadProcessMemory(hProcess, mbi.BaseAddress, buffer.data(), mbi.RegionSize, &bytesRead);

            // if (!result || bytesRead != mbi.RegionSize)
            // {
            //     DWORD dwError = GetLastError();
            //     std::stringstream ss;
            //     ss << "Readed != size: " << dwError << " " << result << " " << bytesRead << " " << mbi.RegionSize
            //        << "\n";
            //     // g_log << ss.str();
            //     switch (dwError)
            //     {
            //     case ERROR_ACCESS_DENIED:
            //         // g_log << "Access denied. Ensure you have appropriate permissions." << std::endl;
            //         break;
            //     case ERROR_INVALID_PARAMETER:
            //         // g_log << "Invalid parameter provided. Check the address or buffer size." << std::endl;
            //         break;
            //     case ERROR_PARTIAL_COPY:
            //         // g_log << "Only part of the memory was read. This may indicate a boundary issue." << std::endl;
            //         break;
            //     default:
            //         // g_log << "Unknown error." << std::endl;
            //         break;
            //     }
            // }
            // else
            // {
            //     dumpSize = bytesRead;
            //     outFile.write(reinterpret_cast<const char *>(buffer.data()), bytesRead);
            // }
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

void MemRead(std::string out_location, Logger& log)
{
    auto threadInfos = GetAllThreadInfo(GetCurrentProcessId());
    std::ofstream outFile(out_location + "threads_dump.csv");
    outFile << "ExitStatus,TebBaseAddress,UniqueProcess,UniqueThread,AffinityMask,Priority,BasePriority\n";
    for (auto threadInfo : threadInfos)
    {
        outFile << threadInfo.ExitStatus << "," << threadInfo.TebBaseAddress << "," <<
        threadInfo.ClientId.UniqueProcess << "," << threadInfo.ClientId.UniqueThread << "," <<
        threadInfo.AffinityMask << "," << threadInfo.Priority << "," << threadInfo.BasePriority << "\n";
    }
    log << "MemReadThread started \n";
    auto sections = dumpWithMapping(out_location + "mem_dump.bin", GetCurrentProcess());
    DumpMemoryMapToCSV(out_location + "mem_map.csv", sections);
    log << "DumpMemoryMapToCSV finished \n";

    std::ofstream csvFile(out_location + "heap_analysis.csv");
    if (csvFile)
    {
        HeapAnalyzer analyzer(csvFile);
        analyzer.Analyze();
        csvFile.close();
        log << "Heap data saved to heap_analysis.csv\n";
    }
    else
    {
        log << "Error: Cannot open CSV file!\n";
    }

    log << "MemReadThread finished \n ";
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