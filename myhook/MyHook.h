#ifndef MYHOOK_H_
#define MYHOOK_H_

#include "common/common.h"
#include "common/utility.h"
#include <atomic>

class MyHook
{
  public:
    static MyHook &getInstance()
    {
        static MyHook instance;
        return instance;
    }
    MyHook();

    void start();
    void stop();

  private:
    ~MyHook() = default;
    MyHook(const MyHook &) = delete;
    MyHook &operator=(const MyHook &) = delete;

    struct HookParams
    {
        // std::wstring targetWindow = L"Lineage2M l Katzman";
        const std::wstring targetWindow = WINDOW_TITLE;
        std::string logDumpLocation = "C:\\Users\\Alex\\Documents\\";
        char hotkey = 'R';
        // char         hotkey = VK_F5;
        bool dumpMemory = false;
        int dumpInterval = 30;
        int keyInterval = 1000;
    } g_params;
    Logger m_log;

    enum Threads
    {
        THREAD_CREATOR,
        MSG_CONSUMER,
        LAST
    };

    std::array<HANDLE, Threads::LAST> m_threadsHandler = {};
    std::atomic<bool> m_running = true;
    std::atomic<bool> m_threadStarted = false;
    HWND m_targetHwnd = nullptr;

    void HandleMessage(const Interface::CommandEnvelope *msg);

    DWORD ThreadsCreator();
    DWORD MsgConsumerThread();

    // static wrappers for CreateThread
    static DWORD WINAPI ThreadWrapperCreator(LPVOID);
    static DWORD WINAPI ThreadWrapperMsg(LPVOID);
};

#endif // MYHOOK_H_
