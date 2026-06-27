#include "Injector.h"
#include <iostream>

Injector::Injector() : m_hModule(NULL), m_hHook(NULL)
{
}

Injector::~Injector()
{
    unhook();
}

bool Injector::hook(const std::wstring &windowTitle, const std::string &dllName)
{
    if (m_hHook || m_hModule)
    {
        std::cerr << "Injector cleanup is still pending.\n";
        return false;
    }

    DWORD tid = getThreadId(windowTitle);
    if (tid == 0)
    {
        std::cerr << "Failed to get thread ID.\n";
        return false;
    }

    std::string dllPath = getDllPath(dllName);
    m_hModule = LoadLibraryA(dllPath.c_str());
    if (!m_hModule)
    {
        std::cerr << "Failed to load DLL.\n";
        return false;
    }

    HOOKPROC addr = (HOOKPROC)GetProcAddress(m_hModule, "HookProc");
    if (!addr)
    {
        std::cerr << "Failed to find HookProc.\n";
        if (FreeLibrary(m_hModule))
        {
            m_hModule = nullptr;
        }
        else
        {
            std::cerr << "FreeLibrary failed. Error: " << GetLastError() << "\n";
        }
        return false;
    }

    m_hHook = SetWindowsHookExA(WH_GETMESSAGE, addr, m_hModule, tid);
    if (!m_hHook)
    {
        std::cerr << "SetWindowsHookEx failed. Error: " << GetLastError() << "\n";
        if (FreeLibrary(m_hModule))
        {
            m_hModule = nullptr;
        }
        else
        {
            std::cerr << "FreeLibrary failed. Error: " << GetLastError() << "\n";
        }
        return false;
    }

    return true;
}

bool Injector::unhook()
{
    if (m_hHook)
    {
        if (!UnhookWindowsHookEx(m_hHook))
        {
            std::cerr << "UnhookWindowsHookEx failed. Error: " << GetLastError() << "\n";
            return false;
        }
        m_hHook = nullptr;
    }
    if (m_hModule)
    {
        if (!FreeLibrary(m_hModule))
        {
            std::cerr << "FreeLibrary failed. Error: " << GetLastError() << "\n";
            return false;
        }
        m_hModule = nullptr;
    }
    return true;
}

DWORD Injector::getThreadId(const std::wstring &windowName)
{
    m_hwnd = FindWindowW(nullptr, windowName.c_str());
    if (!m_hwnd)
        return 0;

    DWORD pid = 0;
    return GetWindowThreadProcessId(m_hwnd, &pid);
}

std::string Injector::getDllPath(const std::string &dllName)
{
    char buffer[MAX_PATH];
    if (!GetModuleFileNameA(NULL, buffer, MAX_PATH))
        return "";

    std::string exePath(buffer);
    size_t pos = exePath.find_last_of("\\/");

    if (pos == std::string::npos)
        return "";

    std::string parentDir = exePath.substr(0, pos);
    size_t slashPos = parentDir.find_last_of("\\/");
    if (slashPos == std::string::npos)
        return dllName;

    std::string dllPath = parentDir.substr(0, slashPos + 1) + dllName;
    return dllPath;
}

bool Injector::isHooked() const
{
    return m_hHook != nullptr || m_hModule != nullptr;
}
