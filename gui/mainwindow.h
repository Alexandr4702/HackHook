#pragma once

#include "WindowSelectorCombo.h"
#include "common/common.h"
#include "common/utility.h"
#include "myhook/MemoryScanner.h"
#include "injector/Injector.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <QMainWindow>
#include <condition_variable>
#include <thread>
#include <vector>
#include "MemoryCache.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void refreshWindowList();
    void MsgConsumerThread();
    void HandleMessage(const Interface::CommandEnvelope *msg);

  private slots:
    void on_windowSelectorOpened();
    void on_hookButton_clicked();
    void on_dumpButton_clicked();
    void on_firstScanButton_clicked();
    void on_nextScanButton_clicked();
    void on_pressKeyButton_clicked();
    void on_clearButton_clicked();

  private:
    void printOccurences(const MemoryCache &occurences);
    void refreshCachedRegions(std::function<void()> done);
    void filterOccurrences(std::span<const uint8_t> value, Interface::ValueType type);

    class RpcClient
    {
        public:
        using Callback = std::function<void(const Interface::CommandEnvelope *)>;

        RpcClient(MessageIPCSender& sender): m_sender(sender)
        {

        }

        template <typename TCreateFn, typename... Args>
        bool send_rpc(Interface::CommandID id, Interface::Command type, TCreateFn create_fn,
                      Args &&...args)
        {
            return m_sender.send_command(m_cnt.fetch_add(1, std::memory_order_relaxed), id, type, create_fn, std::forward<Args>(args)...);
        }
        template <typename TCreateFn, typename... Args>
        bool send_rpc_cb(Callback cb, Interface::CommandID id, Interface::Command type,
                          TCreateFn create_fn, Args &&...args)
        {
            auto cnt = m_cnt.fetch_add(1, std::memory_order_relaxed);
            {
                std::scoped_lock lck(m_mtx);
                m_callbacks[cnt] = std::move(cb);
            }
            if (!m_sender.send_command(cnt, id, type, create_fn, std::forward<Args>(args)...))
            {
                std::scoped_lock lck(m_mtx);
                m_callbacks.erase(cnt);
                return false;
            }
            return true;
        }
        void call_cb(uint64_t request_id, const Interface::CommandEnvelope *msg)
        {
            decltype(m_callbacks)::mapped_type cb;
            {
                std::scoped_lock lck(m_mtx);
                auto it = m_callbacks.find(request_id);
                if(it == m_callbacks.end())
                    return;
                cb = std::move(it->second);
                m_callbacks.erase(it);
            }
            if (cb)
                cb(msg);
        }
        private:
        MessageIPCSender &m_sender;
        std::atomic_uint64_t m_cnt = 0;
        std::mutex m_mtx;
        std::unordered_map<uint64_t, Callback> m_callbacks;
    };

    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
    std::jthread m_recive_thread;
    std::atomic<bool> m_running = true;

    RpcClient m_rpc_client;
    MemoryCache m_occur_storage;
    bool m_hooked = false;
    std::mutex m_hook_mutex;
    std::condition_variable m_hook_cv;
};
