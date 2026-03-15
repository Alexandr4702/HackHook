#pragma once

#include <string>
#include <windows.h>

class Injector
{
  public:
    Injector();
    ~Injector();

    bool hook(const std::wstring &windowTitle, const std::string &dllName = "myhook.dll");
    void unhook();
    bool isHooked() const;
    HWND getHWND() const
    {return m_hwnd;}
  private:
    std::wstring m_windowTitle;
    std::string m_dllName;
    HMODULE m_hModule;
    HHOOK m_hHook;
    HWND m_hwnd;

    DWORD getThreadId(const std::wstring &windowName);
    static std::string getDllPath(const std::string &dllName);
};
