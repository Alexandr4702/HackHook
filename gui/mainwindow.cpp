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
    m_recive_thread = std::thread(&MainWindow::MsgConsumerThread, this);
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this, &MainWindow::onWindowSelectorOpened);
}

MainWindow::~MainWindow()
{
    m_running.store(false, std::memory_order_release);
    m_reciver.close();
    m_recive_thread.join();
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
        uint32_t len = 0;
        std::stringstream ss;
        if (!m_reciver.consume_block(std::span<uint8_t>(reinterpret_cast<uint8_t *>(&len), sizeof(len))))
            break;
        buff.resize(len);
        if (!m_reciver.consume_block(std::span<uint8_t>(buff.data(), len)))
            break;
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
        qDebug() << "[HandleMessage] Received write command with offset: " << msg->body_as_WriteCommand()->offset() << "\n";

        break;
    case CommandID_ACK:
        qDebug() << "[HandleMessage] Received CommandID_ACK command" << msg->body_as_ReadCommand()->offset() << "\n";

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
    if (!m_injector.isHooked())
    {
        std::wstring windowName = ui->windowSelectorCombo->currentText().toStdWString();
        qDebug() << "hookInjected " << windowName;
        if (windowName.empty())
            return;
        if (m_injector.hook(windowName))
        {
            ui->hookButton->setText("Unhook");
            m_sender.reset();
            ui->dumpButton->setEnabled(true);
        }
    }
    else
    {
        m_injector.unhook();
        m_sender.close();
        m_reciver.reset();
        ui->dumpButton->setEnabled(false);
        qDebug() << "unooked";
        ui->hookButton->setText("Inject Hook");
    }
}

void MainWindow::on_dumpButton_clicked()
{
    qDebug() << "Dump button clicked";
    using namespace Interface;


    flatbuffers::FlatBufferBuilder builder;
    uint64_t offset = 1024;
    std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
    auto data_vec = builder.CreateVector(payload);

    auto write_cmd = CreateWriteCommand(builder, offset, data_vec);
    auto envelope = CreateCommandEnvelope(
        builder,
        CommandID_WRITE,
        Command::Command_WriteCommand,
        write_cmd.Union());
    builder.Finish(envelope);


    m_sender.send(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(builder.GetBufferPointer()), builder.GetSize()));
}
