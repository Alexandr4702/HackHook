#pragma once

#include "common/utility.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>

// Correlates IPC replies with requests.  Keeping this transport concern out of
// MainWindow lets the window focus on presentation and hook lifecycle state.
class RpcClient
{
  public:
    using Callback = std::function<void(const Interface::CommandEnvelope *)>;

    explicit RpcClient(MessageIPCSender &sender) : m_sender(sender)
    {
    }

    template <typename TCreateFn, typename... Args>
    bool send_rpc(Interface::CommandID id, Interface::Command type, TCreateFn create_fn,
                  Args &&...args)
    {
        return m_sender.send_command(next_request_id(), id, type, create_fn,
                                     std::forward<Args>(args)...);
    }

    template <typename TCreateFn, typename... Args>
    bool send_rpc_cb(Callback callback, Interface::CommandID id, Interface::Command type,
                     TCreateFn create_fn, Args &&...args)
    {
        const auto requestId = next_request_id();
        {
            std::scoped_lock lock(m_mutex);
            m_callbacks.emplace(requestId, std::move(callback));
        }

        if (m_sender.send_command(requestId, id, type, create_fn,
                                  std::forward<Args>(args)...))
            return true;

        std::scoped_lock lock(m_mutex);
        m_callbacks.erase(requestId);
        return false;
    }

    void call_cb(uint64_t requestId, const Interface::CommandEnvelope *message)
    {
        Callback callback;
        {
            std::scoped_lock lock(m_mutex);
            const auto it = m_callbacks.find(requestId);
            if (it == m_callbacks.end())
                return;

            callback = std::move(it->second);
            m_callbacks.erase(it);
        }

        // Invoke outside the mutex: callbacks may synchronously issue another
        // request or clear pending work during shutdown.
        if (callback)
            callback(message);
    }

    void clear_callbacks()
    {
        std::scoped_lock lock(m_mutex);
        m_callbacks.clear();
    }

  private:
    uint64_t next_request_id()
    {
        return m_nextRequestId.fetch_add(1, std::memory_order_relaxed);
    }

    MessageIPCSender &m_sender;
    std::atomic_uint64_t m_nextRequestId = 0;
    std::mutex m_mutex;
    std::unordered_map<uint64_t, Callback> m_callbacks;
};
