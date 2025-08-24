#pragma once

#include "WindowSelectorCombo.h"
#include "common/common.h"
#include "common/utility.h"
#include "injector/Injector.h"
#include <QMainWindow>
#include <condition_variable>
#include <thread>

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
    void onWindowSelectorOpened();
    void on_hookButton_clicked();
    void on_dumpButton_clicked();
    void on_firstScanButton_clicked();
    void on_nextScanButton_clicked();

  private:
    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
    SharedBuffer m_reciver;
    std::jthread m_recive_thread;
    std::atomic<bool> m_running = true;

    bool m_hooked = false;
    std::mutex m_hook_mutex;
    std::condition_variable m_hook_cv;
};