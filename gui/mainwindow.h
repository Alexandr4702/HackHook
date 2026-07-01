#pragma once

#include "WindowSelectorCombo.h"
#include "RegionViewDialog.h"
#include "RpcClient.h"
#include "common/common.h"
#include "common/utility.h"
#include "myhook/MemoryScanner.h"
#include "injector/Injector.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <QMainWindow>
#include <condition_variable>
#include <thread>
#include <vector>
#include "MemoryCache.h"

class QPoint;
class QCloseEvent;
class QTableWidget;
class QTimer;

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

  protected:
    void closeEvent(QCloseEvent *event) override;

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
    void showResultsContextMenu(const QPoint &position);
    void showValueWatchContextMenu(const QPoint &position);
    void showRegionWatchContextMenu(const QPoint &position);
    bool addValueWatch(const FoundOccurrences &occurrence, bool refresh = true);
    bool addRegionWatch(const FoundOccurrences &occurrence, bool refresh = true);
    void saveTable(QTableWidget *table, const QString &title);
    void refreshWatches();
    void finishWatchRequest();
    void updateValueWatchRow(size_t index, const QString &value, const QString &status);
    void updateRegionWatchRow(size_t index, const MemoryRegionDetails *details, const QString &status);
    void viewRegion(const FoundOccurrences &occurrence, std::vector<FoundOccurrences> regionOccurrences);
    using RegionDataCallback = std::function<void(std::vector<uint8_t>, MemoryRegionDetails)>;
    bool requestRegionData(const FoundOccurrences &occurrence, RegionDataCallback done,
                           std::function<void()> failed = {});
    void finishUnhook();
    void completeUnhook();
    void waitForTargetUnload(uint64_t attempt, int retriesRemaining);
    void finishClose();

    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
    std::jthread m_recive_thread;
    std::atomic<bool> m_running = true;

    RpcClient m_rpc_client;
    MemoryCache m_occur_storage;
    struct RegionWatch
    {
        FoundOccurrences occurrence{};
        MemoryRegionDetails details{};
    };
    std::vector<FoundOccurrences> m_valueWatches;
    std::vector<RegionWatch> m_regionWatches;
    QTimer *m_watchTimer = nullptr;
    size_t m_pendingWatchRequests = 0;
    bool m_watchRefreshInProgress = false;
    bool m_watchRefreshQueued = false;
    bool m_hooked = false;
    bool m_hookReady = false;
    bool m_unhookPending = false;
    bool m_hookStopAcknowledged = false;
    bool m_closePending = false;
    bool m_closeApproved = false;
    bool m_unhookFinalizing = false;
    bool m_finalizeAcknowledged = false;
    uint64_t m_unhookAttempt = 0;
    std::mutex m_hook_mutex;
    std::condition_variable m_hook_cv;
};
