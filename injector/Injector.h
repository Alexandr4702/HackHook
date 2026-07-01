#pragma once

#include <string>
#include <windows.h>

class Injector
{
  public:
    Injector();
    ~Injector();

    bool hook(const std::wstring &windowTitle, const std::string &dllName = "myhook.dll");
    bool unhook();
    bool isHooked() const;
    bool isTargetModuleLoaded() const;
    HWND getHWND() const
    {
        return m_hwnd;
    }

  private:
    std::wstring m_windowTitle;
    std::string m_dllName;
    HMODULE m_hModule;
    HHOOK m_hHook;
    HWND m_hwnd = nullptr;
    DWORD m_targetPid = 0;
    bool m_quiescencePending = false;

    DWORD getThreadId(const std::wstring &windowName);
    bool waitForHookThread();
    static std::string getDllPath(const std::string &dllName);
};
