#ifndef MEM_DUMPER_CPP_
#define MEM_DUMPER_CPP_

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <psapi.h>

#include <vector>
#include <string>
#include <unordered_map>

#include "common/utility.h"

typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PVOID TebBaseAddress;
    CLIENT_ID ClientId;
    ULONG_PTR AffinityMask;
    LONG Priority;
    LONG BasePriority;
} THREAD_BASIC_INFORMATION;

typedef NTSTATUS(NTAPI *pNtQueryInformationThread)(HANDLE ThreadHandle, THREADINFOCLASS ThreadInformationClass,
                                                   PVOID ThreadInformation, ULONG ThreadInformationLength,
                                                   PULONG ReturnLength);

struct SectionRange
{
    uintptr_t start;
    uintptr_t end;
    std::string name;
};

struct ModuleInfo
{
    std::string moduleName;
    LPVOID baseAddress;
    std::vector<SectionRange> sectionRanges;
};

struct SectionInfo
{
    LPVOID baseAddress;
    SIZE_T regionSize;
    size_t dumpOffset;
    size_t dumpSize;
    DWORD state;
    DWORD protect;
    DWORD type;
    std::string moduleName;
    std::string sectionName;
};

void MemRead(std::string out_location, Logger& log);

#endif // MEM_DUMPER_CPP_