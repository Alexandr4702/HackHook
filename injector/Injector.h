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

  private:
    std::wstring m_windowTitle;
    std::string m_dllName;
    HMODULE m_hModule;
    HHOOK m_hHook;

    static DWORD getThreadId(const std::wstring &windowName);
    static std::string getDllPath(const std::string &dllName);
};
