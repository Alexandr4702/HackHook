#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <atomic>
#include <thread>
#include <windows.h>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_sender(BUFFER_NAME_TX, true), m_reciver(BUFFER_NAME_RX, true)
{
    ui->setupUi(this);
    ui->dumpButton->setEnabled(false);
    m_recive_thread = std::jthread(&MainWindow::MsgConsumerThread, this);
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this, &MainWindow::onWindowSelectorOpened);
}

MainWindow::~MainWindow()
{
    if (m_injector.isHooked())
    {
        m_injector.unhook();
        m_sender.close();
        m_reciver.reset();
        m_hooked = false;
    }

    m_running.store(false, std::memory_order_release);
    m_reciver.close();
    m_hook_cv.notify_all();

    delete ui;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
    if (!IsWindowVisible(hwnd))
        return TRUE;

    int length = GetWindowTextLengthW(hwnd);
    if (length == 0)
        return TRUE;

    std::wstring title(length, L'\0');
    GetWindowTextW(hwnd, &title[0], length + 1);

    auto comboBox = reinterpret_cast<QComboBox *>(lParam);
    comboBox->addItem(QString::fromStdWString(title));
    return TRUE;
}

void MainWindow::MsgConsumerThread()
{
    using namespace Interface;
    std::vector<uint8_t> buff;
    buff.resize(4);

    while (m_running.load(std::memory_order_acquire))
    {
        {
            std::unique_lock lck(m_hook_mutex);
            m_hook_cv.wait(lck, [this]() { return m_hooked || !m_running.load(std::memory_order_acquire); });
        }

        if (!m_running.load(std::memory_order_acquire)) 
        {
            break;
        }

        uint32_t len = 0;
        if (!m_reciver.consume_block(std::span<uint8_t>(reinterpret_cast<uint8_t *>(&len), sizeof(len))))
        {
            qDebug() << "Failed to consume len";
            continue;
        }
        buff.resize(len);
        if (!m_reciver.consume_block(std::span<uint8_t>(buff.data(), len)))
        {
            qDebug() << "Failed to consume data";
            continue;
        }
        HandleMessage(GetCommandEnvelope(buff.data()));
    }

    return;
}

void MainWindow::HandleMessage(const Interface::CommandEnvelope *msg)
{
    using namespace Interface;
    if (msg == nullptr)
        return;

    switch (msg->id())
    {
    case CommandID_WRITE:
        qDebug() << "[HandleMessage] Received write command with offset: " << msg->body_as_WriteCommand()->offset();
        break;
    case CommandID_ACK:
        qDebug() << "[HandleMessage] Received CommandID_ACK command";
        break;
    default:
        break;
    }
}

void MainWindow::refreshWindowList()
{
    ui->windowSelectorCombo->clear();
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(ui->windowSelectorCombo));
}

void MainWindow::onWindowSelectorOpened()
{
    ui->windowSelectorCombo->clear();

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
            if (!IsWindowVisible(hwnd))
                return TRUE;

            int length = GetWindowTextLengthW(hwnd);
            if (length == 0)
                return TRUE;

            std::wstring title(length, L'\0');
            GetWindowTextW(hwnd, &title[0], length + 1);

            auto combo = reinterpret_cast<QComboBox *>(lParam);
            combo->addItem(QString::fromStdWString(title));
            return TRUE;
        },
        reinterpret_cast<LPARAM>(ui->windowSelectorCombo));
}

void MainWindow::on_hookButton_clicked()
{
    std::scoped_lock lck(m_hook_mutex);
    if (!m_injector.isHooked())
    {
        std::wstring windowName = ui->windowSelectorCombo->currentText().toStdWString();
        if (windowName.empty())
        {
            qDebug() << "Window name is empty " << windowName;
            return;
        }
        if (m_injector.hook(windowName))
        {
            qDebug() << "HookInjected " << windowName;
            ui->hookButton->setText("Unhook");
            m_sender.reset();
            m_reciver.reset();
            m_hooked = true;
            m_hook_cv.notify_all();
            ui->dumpButton->setEnabled(true);
        } 
        else {
            qDebug() << "Hook is not injected " << windowName;
        }
    }
    else
    {
        m_injector.unhook();
        m_sender.close();
        m_reciver.reset();
        m_hooked = false;
        ui->dumpButton->setEnabled(false);
        qDebug() << "unooked";
        ui->hookButton->setText("Inject Hook");
    }
}

void MainWindow::on_dumpButton_clicked()
{
    qDebug() << "Dump button clicked";
    using namespace Interface;

    m_sender.send_command(Interface::CommandID::CommandID_DUMP, Interface::Command::Command_NONE, CreateEmptyCommand);
}
