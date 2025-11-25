#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <atomic>
#include <thread>
#include <windows.h>

#include "myhook/MemoryScanner.h"

namespace
{

    constexpr std::array<std::pair<const char*, Interface::ValueType>, Interface::ValueType_LAST> valueTypes {{
        {"Int32",     Interface::ValueType_Int32},
        {"Float",     Interface::ValueType_Float},
        {"Double",    Interface::ValueType_Double},
        {"Int64",     Interface::ValueType_Int64},
        {"String",    Interface::ValueType_String},
        {"String16",  Interface::ValueType_String16},
        {"ByteArray", Interface::ValueType_ByteArray}
    }};
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow)
{
    m_sender.init(BUFFER_NAME_TX, true);
    m_reciver.init(BUFFER_NAME_RX, BUFFER_CAPACITY, true);
    ui->setupUi(this);
    ui->dumpButton->setEnabled(false);
    for (const auto &[text, type] : valueTypes)
    {
        ui->valueTypeCombo->addItem(text, static_cast<int>(type));
    }
    m_recive_thread = std::jthread(&MainWindow::MsgConsumerThread, this);
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this, &MainWindow::onWindowSelectorOpened);
}

MainWindow::~MainWindow()
{
    m_running.store(false, std::memory_order_release);
    m_reciver.close();
    {
        std::scoped_lock lck(m_hook_mutex);
        m_hooked = false;
        m_hook_cv.notify_all();
    }
    if (m_recive_thread.joinable())
    {
        m_recive_thread.join();
    }
    m_sender.close();
    
    if (m_injector.isHooked())
    {
        m_injector.unhook();
    }
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
    if (msg == nullptr)
        return;

    switch (msg->id())
    {
    case Interface::CommandID_WRITE:
        qDebug() << "[HandleMessage] Received write command with offset: " << msg->body_as_WriteCommand()->offset();
        break;
    case Interface::CommandID_ACK:
        qDebug() << "[HandleMessage] Received CommandID_ACK command";
        break;
    case Interface::CommandID_FIND_ACK:
    {
        ui->resultsTable->setRowCount(0);
        auto occurrences = msg->body_as_FindAck()->occurrences();
        auto value_type = msg->body_as_FindAck()->value_type();
        auto value = valueToString(msg->body_as_FindAck()->value(), msg->body_as_FindAck()->value_type());

        for (auto occurrence : *occurrences)
        {
            FoundOccurrences found;
            found.baseAddress = occurrence->base_address();
            found.offset = occurrence->offset();
            found.region_size = occurrence->region_size();
            found.data_size = occurrence->data_size();
            found.type = occurrence->type();

            auto table = ui->resultsTable;
            int row = table->rowCount();
            table->insertRow(row);

            table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(value)));
            table->setItem(row, 1, new QTableWidgetItem("0x" + QString::number(found.baseAddress, 16))); // hex
            table->setItem(row, 2, new QTableWidgetItem(QString::number(found.offset)));
            table->setItem(row, 3, new QTableWidgetItem(QString::number(found.region_size)));
            table->setItem(row, 4, new QTableWidgetItem(QString::number(found.data_size)));
            table->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(valueTypes[value_type].first)));
            table->setItem(row, 6, new QTableWidgetItem(""));
        }
        break;
    }
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
        m_sender.close();
        m_reciver.reset();
        m_hooked = false;
        m_injector.unhook();
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

void MainWindow::on_firstScanButton_clicked()
{
    using namespace Interface;
    if (!m_injector.isHooked())
    {
        return;
    }
    // ui->valueTypeCombo->currentData().toInt();
    std::vector<uint8_t> data;
    auto value_type = ui->valueTypeCombo->currentData().toInt();
    bool ok = true;

    switch (value_type)
    {
    case ValueType::ValueType_Int32: {
        int32_t val = ui->valueEdit->text().toInt(&ok);
        uint8_t *p = reinterpret_cast<uint8_t *>(&val);
        data.assign(p, p + sizeof(val));
        break;
    }
    case ValueType::ValueType_Float: {
        float val = ui->valueEdit->text().toFloat(&ok);
        uint8_t *p = reinterpret_cast<uint8_t *>(&val);
        data.assign(p, p + sizeof(val));
        break;
    }
    case ValueType::ValueType_Double: {
        double val = ui->valueEdit->text().toDouble(&ok);
        uint8_t *p = reinterpret_cast<uint8_t *>(&val);
        data.assign(p, p + sizeof(val));
        break;
    }
    case ValueType::ValueType_Int64: {
        qint64 val = ui->valueEdit->text().toLongLong(&ok);
        uint8_t *p = reinterpret_cast<uint8_t *>(&val);
        data.assign(p, p + sizeof(val));
        break;
    }
    case ValueType::ValueType_String: {
        QByteArray val = ui->valueEdit->text().toUtf8();
        data.assign(val.begin(), val.end());
        break;
    }
    case ValueType::ValueType_String16: {
        auto str = ui->valueEdit->text().toStdU16String();
        uint8_t *p = reinterpret_cast<uint8_t *>(str.data());
        data.assign(p, p + str.size() * sizeof(char16_t));
        break;
    }
    case ValueType::ValueType_ByteArray: {
        std::string hex = ui->valueEdit->text().toStdString();

        if (hex.size() % 2 != 0)
        {
            hex = "0" + hex;
        }
        for (size_t i = 0; i < hex.size(); i += 2)
        {
            uint8_t byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
            data.push_back(byte);
        }
        break;
    }
    default:
        break;
    }
    qDebug() << data;
    if(!ok)
    {
        qDebug() << "Failed to parse value";
        return;
    }
    // ui->valueEdit->text();

    if (data.size() == 0)
    {
        return;
    }

    auto create_find_cmd = [](flatbuffers::FlatBufferBuilder &builder, Interface::ValueType type, const std::vector<uint8_t> &data) {
        auto vec = builder.CreateVector(data);
        return Interface::CreateFindCommand(builder, type, vec);
    };

    m_sender.send_command(Interface::CommandID::CommandID_FIND, Interface::Command::Command_FindCommand, create_find_cmd, static_cast<Interface::ValueType> (value_type), data);
    qDebug() << "on_firstScan_clicked " << ui->valueTypeCombo->currentData().toInt() << " " << ui->valueEdit->text();
}

void MainWindow::on_nextScanButton_clicked()
{
    if (!m_injector.isHooked())
    {
        return;
    }

    qDebug() << "on_nextScan_clicked";
}
