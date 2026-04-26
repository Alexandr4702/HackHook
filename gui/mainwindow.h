#pragma once

#include "WindowSelectorCombo.h"
#include "common/common.h"
#include "common/utility.h"
#include "myhook/MemoryScanner.h"
#include "injector/Injector.h"
#include <unordered_map>
#include <utility>
#include <QMainWindow>
#include <condition_variable>
#include <thread>
#include <vector>

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

  private:
    class OccurrencesStorage;
    void printOccurences(const OccurrencesStorage& occurences);
    // void printOccurences(std::vector<std::pair<FoundOccurrences, std::vector<uint8_t>>>& occurences);

    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
    std::jthread m_recive_thread;
    std::atomic<bool> m_running = true;

    class OccurrencesStorage
    {
      public:
        template <typename T, typename U>
            requires std::same_as<std::remove_cvref_t<T>, std::vector<uint8_t>> &&
                     std::same_as<std::remove_cvref_t<U>, FoundOccurrences>
        void put(T &&data, U &&occur)
        {
            auto [it, _] = s.try_emplace(std::forward<T>(data));
            auto &key = it->first;

            auto &occurrences = it->second;
            auto [occIt, inserted] = occurrences.insert(std::forward<U>(occur));

            if (inserted)
            {
                viewSortedItems.emplace(*occIt, std::cref(key));
            }
        }

        void erase(const std::vector<uint8_t> &data)
        {
            auto it = s.find(data);
            if (it == s.end())
                return;

            auto &key = it->first;

            for (const auto &occur : it->second)
            {
                auto range = viewSortedItems.equal_range({occur, std::cref(key)});
                for (auto sit = range.first; sit != range.second; ++sit)
                {
                    if (&sit->second.get() == &key)
                    {
                        viewSortedItems.erase(sit);
                        break;
                    }
                }
            }

            s.erase(it);
        }

        void erase(const std::vector<uint8_t> &data, const FoundOccurrences &occur)
        {
            auto it = s.find(data);
            if (it == s.end())
                return;

            auto &key = it->first;

            it->second.erase(occur);

            auto range = viewSortedItems.equal_range({occur, std::cref(key)});
            for (auto sit = range.first; sit != range.second; ++sit)
            {
                if (&sit->second.get() == &key)
                {
                    viewSortedItems.erase(sit);
                    break;
                }
            }

            if (it->second.empty())
                s.erase(it);
        }

        auto getFirst() const
        {
            return std::ranges::subrange(viewSortedItems.begin(), viewSortedItems.end());
        }

      private:
        struct VectorHash
        {
            size_t operator()(const std::vector<uint8_t> &v) const noexcept
            {
                size_t h = 0;
                for (auto b : v)
                    h = h * 31 + b;
                return h;
            }
        };

        using SortedItem = std::pair<FoundOccurrences, std::reference_wrapper<const std::vector<uint8_t>>>;

        struct CompareItem
        {
            bool operator()(const SortedItem &a, const SortedItem &b) const noexcept
            {
                if (a.first < b.first)
                    return true;
                if (b.first < a.first)
                    return false;
                return &a.second.get() < &b.second.get();
            }
        };

        std::unordered_map<std::vector<uint8_t>, std::set<FoundOccurrences>, VectorHash> s;
        std::multiset<SortedItem, CompareItem> viewSortedItems;
    };

    OccurrencesStorage m_occur_storage;
    bool m_hooked = false;
    std::mutex m_hook_mutex;
    std::condition_variable m_hook_cv;
};