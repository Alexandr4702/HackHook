#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <string>
#include <mutex>
#include "common/common.h"
#include "common/utility.h"

using namespace Interface;

DWORD GetThreadIdByWindowName(const std::wstring &windowName)
{
    HWND hwnd = FindWindowW(nullptr, windowName.c_str());
    if (!hwnd)
        return 0;

    DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
    return tid;
}

std::string GetDllPath()
{
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);

    std::string fullPath(buffer);
    size_t pos = fullPath.find_last_of("\\/");
    return fullPath.substr(0, pos + 1) + "myhook.dll";
}

void print(std::span<const uint8_t> bytes)
{
    for (byte b : bytes)
    {
        std::cout << std::format("{:02x} ", b);
    }
}

int main()
{

    DWORD threadId = GetThreadIdByWindowName(WINDOW_TITLE);
    if (threadId == 0)
    {
        std::cerr << "Failed to get thread ID.\n";
        return 1;
    }

    std::string dllPath = GetDllPath();

    HMODULE hModule = LoadLibraryA(dllPath.c_str());
    if (!hModule)
    {
        std::cerr << "Failed to load DLL into injector process.\n";
        return 1;
    }

    HOOKPROC addr = (HOOKPROC)GetProcAddress(hModule, "HookProc");
    if (!addr)
    {
        std::cerr << "Failed to find HookProc in DLL.\n";
        FreeLibrary(hModule);
        return 1;
    }

    HHOOK hHook = SetWindowsHookEx(WH_GETMESSAGE, addr, hModule, threadId);
    if (!hHook)
    {
        DWORD errorCode = GetLastError();
        std::cerr << "SetWindowsHookEx failed. Error code: " << errorCode << std::endl;
        FreeLibrary(hModule);
        return 1;
    }

    std::cout << "Hook installed. Press Enter to unhook...\n";

    MessageIPCSender sender(BUFFER_NAME_TX, true);

    flatbuffers::FlatBufferBuilder builder;
    uint64_t offset = 1024;
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
    auto data_vec = builder.CreateVector(payload);

    auto write_cmd = CreateWriteCommand(builder, offset, data_vec);
    auto envelope = CreateCommandEnvelope(
        builder,
        CommandID_WRITE,
        Command::Command_WriteCommand,
        write_cmd.Union());
    builder.Finish(envelope);

    sender.send(
        std::span<const uint8_t>(
            reinterpret_cast<const uint8_t *>(builder.GetBufferPointer()),
            builder.GetSize()));

    std::cin.get();

    UnhookWindowsHookEx(hHook);
    FreeLibrary(hModule);

    return 0;
}