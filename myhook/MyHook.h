#ifndef MYHOOK_H_
#define MYHOOK_H_

#include "common/common.h"
#include "common/utility.h"
#include <atomic>
#include "MemoryScanner.h"

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
    ~MyHook() noexcept;

    void start() noexcept;
    void stop();

    Logger m_log;
  private:
    MyHook(const MyHook &) = delete;
    MyHook &operator=(const MyHook &) = delete;

    struct HookParams
    {
        std::string logDumpLocation;
    } g_params;

    struct WorkerContext
    {
        MyHook *hook;
        HMODULE module;
    };

    enum class State : uint8_t
    {
        Stopped,
        Starting,
        Running,
        Stopping,
    };

    std::atomic<bool> m_running = false;
    std::atomic<State> m_state = State::Stopped;
    HWND m_targetHwnd = nullptr;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
    std::optional<MemTool> m_memTool;
  
    const size_t allocate_size = 128 * 1024 * 1024;
    void* m_pmrPoolMem = nullptr;
    std::pmr::monotonic_buffer_resource m_monotonicPool;
    std::pmr::synchronized_pool_resource m_pool;

    void HandleMessage(const Interface::CommandEnvelope *msg);

    DWORD MsgConsumerThread();

    static DWORD WINAPI ThreadWrapperMsg(LPVOID) noexcept;
};

#endif // MYHOOK_H_
