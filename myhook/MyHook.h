#ifndef MYHOOK_H_
#define MYHOOK_H_

#include "common/common.h"
#include "common/utility.h"
#include <atomic>
#include <vector>
#include <thread>

#include <memory_resource>

class MyHook
{
  public:
    static MyHook &getInstance()
    {
        static MyHook instance;
        return instance;
    }
    MyHook();
    ~MyHook();

    void start();
    void stop();

    Logger m_log;
  private:
    MyHook(const MyHook &) = delete;
    MyHook &operator=(const MyHook &) = delete;

    struct HookParams
    {
      // TODO get logDumpLocation from injector
        std::string logDumpLocation = "C:\\Users\\Alex\\Documents\\";
    } g_params;

    std::vector<std::jthread> m_threadsHandler;
    std::atomic<bool> m_running = true;
    std::atomic<bool> m_threadStarted = false;
    HWND m_targetHwnd = nullptr;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
  
    const size_t allocate_size = 128 * 1024 * 1024;
    void* m_pmrPoolMem = nullptr;
    std::pmr::monotonic_buffer_resource m_monotonicPool;
    std::pmr::synchronized_pool_resource m_pool;

    void HandleMessage(const Interface::CommandEnvelope *msg);

    DWORD ThreadsCreator();
    DWORD MsgConsumerThread();

    // static wrappers for CreateThread
    static DWORD WINAPI ThreadWrapperCreator(LPVOID);
    static DWORD WINAPI ThreadWrapperMsg(LPVOID);
};

#endif // MYHOOK_H_
