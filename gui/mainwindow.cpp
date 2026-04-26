#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <atomic>
#include <thread>
#include <vector>
#include <windows.h>


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
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this, &MainWindow::on_windowSelectorOpened);
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
        auto* ack = msg->body_as_FindAck();
        auto value = ack->value();
        auto occurrences = ack->occurrences();
        auto value_type = ack->value_type();
        std::vector<uint8_t> data(value->data(), value->data() + value->size());

        for(auto occur: *occurrences)
        {
            FoundOccurrences found;
            found.baseAddress = occur->base_address();
            found.offset = occur->offset();
            found.region_size = occur->region_size();
            found.data_size = occur->data_size();
            found.type = occur->type();

            m_occur_storage.put(data, std::move(found));
        }
        printOccurences(m_occur_storage);
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

void MainWindow::on_windowSelectorOpened()
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
            DWORD pid = 0;
            GetWindowThreadProcessId(m_injector.getHWND(), &pid);
            qDebug() << std::format(L"HookInjected {} {} {}", windowName, reinterpret_cast<uintptr_t>(m_injector.getHWND()), pid);
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

void MainWindow::on_pressKeyButton_clicked()
{
    if (!m_injector.isHooked())
    {
        return;
    }
    uint64_t hwnd = reinterpret_cast<uint64_t>(m_injector.getHWND());
    m_sender.send_command(Interface::CommandID::CommandID_PRESS_KEY, Interface::Command::Command_PressKeyCommand,
                          Interface::CreatePressKeyCommand, hwnd, 'R');
}

void MainWindow::printOccurences(const OccurrencesStorage &occurences)
{
    auto view = occurences.getFirst();

    auto *table = ui->resultsTable;

    table->setRowCount(static_cast<int>(std::ranges::distance(view)));
    int row = 0;

    for (const auto &[occur, dataRef] : view)
    {
        const auto &data = dataRef.get();

        auto valStr = valueToString(std::span<const uint8_t>(data.data(), data.size()),
                                    static_cast<Interface::ValueType>(occur.type));

        table->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(valStr)));
        table->setItem(row, 1, new QTableWidgetItem(QString("0x%1").arg(occur.baseAddress + occur.offset, 0, 16)));
        table->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(occur.baseAddress, 0, 16)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(occur.offset)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(occur.region_size)));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(occur.data_size)));
        table->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(valueTypes[occur.type].first)));
        table->setItem(row, 7, new QTableWidgetItem{});
        ++row;
    }
}
