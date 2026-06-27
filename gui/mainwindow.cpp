#include "mainwindow.h"
#include "MemoryCache.h"
#include "interface_generated.h"
#include "ui_mainwindow.h"
#include <QMetaObject>
#include <QTableWidgetItem>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
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

    std::vector<uint8_t> string2data(const QString &text, int value_type)
    {
        using namespace Interface;

        std::vector<uint8_t> data;
        const QByteArray utf8 = text.toUtf8();
        const std::string_view input(utf8.constData(), static_cast<size_t>(utf8.size()));

        auto parse_value = [input, &data](auto &value) {
            const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), value);
            if (error != std::errc{} || end != input.data() + input.size())
                return false;

            const auto *bytes = reinterpret_cast<const uint8_t *>(&value);
            data.assign(bytes, bytes + sizeof(value));
            return true;
        };

        switch (value_type)
        {
        case ValueType::ValueType_Int32: {
            int32_t val = 0;
            if (!parse_value(val))
                return {};
            break;
        }
        case ValueType::ValueType_Float: {
            float val = 0;
            if (!parse_value(val))
                return {};
            break;
        }
        case ValueType::ValueType_Double: {
            double val = 0;
            if (!parse_value(val))
                return {};
            break;
        }
        case ValueType::ValueType_Int64: {
            int64_t val = 0;
            if (!parse_value(val))
                return {};
            break;
        }
        case ValueType::ValueType_String: {
            data.assign(input.begin(), input.end());
            break;
        }
        case ValueType::ValueType_String16: {
            auto str = text.toStdU16String();
            uint8_t *p = reinterpret_cast<uint8_t *>(str.data());
            data.assign(p, p + str.size() * sizeof(char16_t));
            break;
        }
        case ValueType::ValueType_ByteArray: {
            std::string hex(input);

            if (hex.size() % 2 != 0)
            {
                hex.insert(hex.begin(), '0');
            }
            for (size_t i = 0; i < hex.size(); i += 2)
            {
                unsigned int value = 0;
                const char *begin = hex.data() + i;
                const char *end = begin + 2;
                const auto [parsed_end, error] = std::from_chars(begin, end, value, 16);
                if (error != std::errc{} || parsed_end != end)
                    return {};
                data.push_back(static_cast<uint8_t>(value));
            }
            break;
        }
        default:
            break;
        }
        return data;
    }
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), m_rpc_client(m_sender)
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

    std::wstring title(length + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
    title.resize(copied);

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
        if (len == 0 || len > BUFFER_CAPACITY - sizeof(len))
        {
            qWarning() << "Invalid IPC message length:" << len;
            break;
        }
        buff.resize(len);
        if (!m_reciver.consume_block(std::span<uint8_t>(buff.data(), len)))
        {
            qDebug() << "Failed to consume data";
            continue;
        }
        flatbuffers::Verifier verifier(buff.data(), buff.size());
        if (!VerifyCommandEnvelopeBuffer(verifier))
        {
            qWarning() << "Invalid FlatBuffer message";
            continue;
        }
        auto msg_data = std::make_shared<std::vector<uint8_t>>(buff);
        QMetaObject::invokeMethod(
            this,
            [this, msg_data]() {
                HandleMessage(GetCommandEnvelope(msg_data->data()));
            },
            Qt::QueuedConnection);
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
        if (const auto *cmd = msg->body_as_WriteCommand())
            qDebug() << "[HandleMessage] Received write command with offset: " << cmd->offset();
        break;
    case Interface::CommandID_ACK:
        qDebug() << "[HandleMessage] Received CommandID_ACK command";
        m_rpc_client.call_cb(msg->request_id(), msg);
        break;
    case Interface::CommandID_READ_ACK:
        qDebug() << "[HandleMessage] Received CommandID_READ_ACK command";
        m_rpc_client.call_cb(msg->request_id(), msg);
        break;
    case Interface::CommandID_NACK:
        qDebug() << "[HandleMessage] Received CommandID_NACK command";
        m_rpc_client.call_cb(msg->request_id(), msg);
        break;
    case Interface::CommandID_FIND_ACK:
    {
        auto* ack = msg->body_as_FindAck();
        if (!ack || !ack->value() || !ack->occurrences())
        {
            qWarning() << "Invalid FIND_ACK payload";
            break;
        }
        auto value = ack->value();
        auto occurrences = ack->occurrences();
        auto value_type = ack->value_type();
        std::vector<uint8_t> data(value->data(), value->data() + value->size());
        qDebug() << std::format(L"CommandID_FIND_ACK recived found {} occurrences ", occurrences->size());
        for(auto occur: *occurrences)
        {
            FoundOccurrences found;
            found.baseAddress = occur->base_address();
            found.offset = occur->offset();
            found.region_size = occur->region_size();
            found.data_size = occur->data_size();
            found.type = occur->type();

            auto base = occur->base_address() + occur->offset();
            auto size =  occur->data_size();
            auto type = static_cast<Interface::ValueType>(occur->type());

            MemoryCache::View v{.range = {base, size}, .type = type};

            m_occur_storage.put(data, std::move(v));
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

            std::wstring title(length + 1, L'\0');
            const int copied = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
            title.resize(copied);

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
            m_unhookPending = false;
            m_hook_cv.notify_all();
            ui->dumpButton->setEnabled(true);
        } 
        else {
            qDebug() << "Hook is not injected " << windowName;
        }
    }
    else
    {
        if (m_unhookPending)
            return;

        m_unhookPending = true;
        ui->hookButton->setEnabled(false);

        const bool sent = m_rpc_client.send_rpc_cb(
            [this](const Interface::CommandEnvelope *msg) {
                if (!msg || msg->id() != Interface::CommandID_ACK)
                {
                    m_unhookPending = false;
                    ui->hookButton->setEnabled(true);
                    return;
                }
                finishUnhook();
            },
            Interface::CommandID_STOP, Interface::Command::Command_NONE, Interface::CreateEmptyCommand);

        if (!sent)
        {
            m_unhookPending = false;
            ui->hookButton->setEnabled(true);
        }
    }
}

void MainWindow::finishUnhook()
{
    {
        std::scoped_lock lock(m_hook_mutex);
        m_hooked = false;
        m_hook_cv.notify_all();
    }

    m_sender.close();
    m_reciver.close();
    m_injector.unhook();

    m_unhookPending = false;
    ui->dumpButton->setEnabled(false);
    ui->hookButton->setText("Inject Hook");
    ui->hookButton->setEnabled(true);
    qDebug() << "unhooked";
}

void MainWindow::on_dumpButton_clicked()
{
    if (m_unhookPending || !m_injector.isHooked())
        return;

    qDebug() << "Dump button clicked";
    using namespace Interface;

    m_rpc_client.send_rpc(Interface::CommandID::CommandID_DUMP, Interface::Command::Command_NONE, CreateEmptyCommand);
}

void MainWindow::on_firstScanButton_clicked()
{
    using namespace Interface;
    if (m_unhookPending || !m_injector.isHooked())
    {
        return;
    }

    auto value_type = ui->valueTypeCombo->currentData().toInt();
    const auto &text = ui->valueEdit->text();
    std::vector<uint8_t> data = string2data(text, value_type);

    qDebug() << data;
    if (data.size() == 0)
    {
        qDebug() << "Failed to parse value";
        return;
    }

    auto create_find_cmd = [](flatbuffers::FlatBufferBuilder &builder, Interface::ValueType type, const std::vector<uint8_t> &data) {
        auto vec = builder.CreateVector(data);
        return Interface::CreateFindCommand(builder, type, vec);
    };

    m_rpc_client.send_rpc(Interface::CommandID::CommandID_FIND, Interface::Command::Command_FindCommand, create_find_cmd, static_cast<Interface::ValueType> (value_type), data);
    qDebug() << "on_firstScan_clicked " << ui->valueTypeCombo->currentData().toInt() << " " << text;
}

void MainWindow::on_nextScanButton_clicked()
{
    if (m_unhookPending || !m_injector.isHooked())
    {
        return;
    }
    auto value_type = ui->valueTypeCombo->currentData().toInt();
    const auto &text = ui->valueEdit->text();
    std::vector<uint8_t> data = string2data(text, value_type);

    if (data.empty())
    {
        qDebug() << "Failed to parse value";
        return;
    }

    const auto type = static_cast<Interface::ValueType>(value_type);
    refreshCachedRegions([this, data = std::move(data), type]() {
        filterOccurrences(data, type);
        printOccurences(m_occur_storage);
        qDebug() << "on_nextScan_clicked";
    });
}

void MainWindow::refreshCachedRegions(std::function<void()> done)
{
    std::vector<MemoryCache::RegionRange> regions;
    regions.reserve(m_occur_storage.regions().size());
    for (const auto &region : m_occur_storage.regions())
    {
        regions.push_back(region.range);
    }

    if (regions.empty())
    {
        done();
        return;
    }

    struct RefreshState
    {
        size_t pending = 0;
        std::function<void()> done;
    };

    auto state = std::make_shared<RefreshState>();
    state->pending = regions.size();
    state->done = std::move(done);

    auto finish_one = [this, state]() {
        if (state->pending == 0)
            return;

        --state->pending;
        if (state->pending != 0)
            return;

        if (state->done)
            state->done();
    };

    for (const auto &range : regions)
    {
        const bool sent = m_rpc_client.send_rpc_cb(
            [this, range, finish_one](const Interface::CommandEnvelope *msg) {
                if (msg && msg->id() == Interface::CommandID_READ_ACK)
                {
                    const auto *ack = msg->body_as_ReadAck();
                    const auto *read_data = ack ? ack->data() : nullptr;
                    if (read_data && read_data->size() == range.size)
                    {
                        m_occur_storage.update_data(
                            range, std::vector<uint8_t>(read_data->data(), read_data->data() + read_data->size()));
                    }
                }
                finish_one();
            },
            Interface::CommandID_READ, Interface::Command::Command_ReadCommand, Interface::CreateReadCommand, range.base,
            static_cast<uint64_t>(range.size));

        if (!sent)
        {
            finish_one();
        }
    }
}

void MainWindow::filterOccurrences(std::span<const uint8_t> value, Interface::ValueType type)
{
    std::vector<MemoryCache::View> views;
    views.reserve(m_occur_storage.views().size());
    for (const auto &view : m_occur_storage.views())
    {
        views.push_back(view);
    }

    for (const auto &view : views)
    {
        auto cached_data = m_occur_storage.data(view.range);
        const bool matches =
            view.type == type && cached_data && cached_data->size() >= value.size() &&
            std::ranges::equal(value, std::span<const uint8_t>(cached_data->data(), value.size()));

        if (!matches)
        {
            m_occur_storage.remove_view(view);
        }
    }
}

void MainWindow::on_pressKeyButton_clicked()
{
    if (m_unhookPending || !m_injector.isHooked())
    {
        return;
    }
    uint64_t hwnd = reinterpret_cast<uint64_t>(m_injector.getHWND());
    m_rpc_client.send_rpc(Interface::CommandID::CommandID_PRESS_KEY, Interface::Command::Command_PressKeyCommand,
                          Interface::CreatePressKeyCommand, hwnd, 'R');
}

void MainWindow::on_clearButton_clicked()
{
    m_occur_storage.clear();
    printOccurences(m_occur_storage);
}

void MainWindow::printOccurences(const MemoryCache &occurences)
{
    auto *table = ui->resultsTable;
    table->setSortingEnabled(false);
    table->clearContents();
    table->setRowCount(static_cast<int>(occurences.views().size()));

    int row = 0;
    for (const auto &view : occurences.views())
    {
        auto data = occurences.data(view.range);
        if (!data)
            continue;

        auto region_it = std::ranges::find_if(occurences.regions(), [&view](const MemoryCache::Region &region) {
            return region.range.contains(view.range);
        });

        const uint64_t region_base = region_it != occurences.regions().end() ? region_it->range.base : view.range.base;
        const size_t region_size = region_it != occurences.regions().end() ? region_it->range.size : view.range.size;
        const uint64_t region_offset = view.range.base - region_base;
        const auto type_index = static_cast<size_t>(view.type);
        const char *type_text = type_index < valueTypes.size() ? valueTypes[type_index].first : "Unknown";

        table->setItem(row, 0,
                       new QTableWidgetItem(QString::fromStdString(valueToString(std::span<const uint8_t>(*data), view.type))));
        table->setItem(row, 1, new QTableWidgetItem(QString("0x%1").arg(view.range.base, 0, 16)));
        table->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(region_base, 0, 16)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(region_offset)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(region_size)));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(view.range.size)));
        table->setItem(row, 6, new QTableWidgetItem(QString::fromUtf8(type_text)));
        table->setItem(row, 7, new QTableWidgetItem{});
        ++row;
    }

    table->setRowCount(row);
    table->setSortingEnabled(true);
}
