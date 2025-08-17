#ifndef MYHOOK_H_
#define MYHOOK_H_

#include "common/common.h"
#include "common/utility.h"
#include <atomic>
#include <vector>
#include <thread>

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
      // TODO get logDumpLocation from injector
        std::string logDumpLocation = "C:\\Users\\Alex\\Documents\\";
    } g_params;
    Logger m_log;

    std::vector<std::jthread> m_threadsHandler;
    std::atomic<bool> m_running = true;
    std::atomic<bool> m_threadStarted = false;
    HWND m_targetHwnd = nullptr;
    MessageIPCSender m_sender;
    SharedBuffer<BUFFER_CAPACITY> m_reciver;

    void HandleMessage(const Interface::CommandEnvelope *msg);

    DWORD ThreadsCreator();
    DWORD MsgConsumerThread();

    // static wrappers for CreateThread
    static DWORD WINAPI ThreadWrapperCreator(LPVOID);
    static DWORD WINAPI ThreadWrapperMsg(LPVOID);
};

#endif // MYHOOK_H_
