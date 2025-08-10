#pragma once

#include "WindowSelectorCombo.h"
#include "common/utility.h"
#include "common/common.h"
#include "injector/Injector.h"
#include <QMainWindow>
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

  private:
    Ui::MainWindow *ui;
    Injector m_injector;
    MessageIPCSender m_sender;
    SharedBuffer<BUFFER_CAPACITY> m_reciver;
    std::thread m_recive_thread;
    std::atomic<bool> m_running = true;
};
