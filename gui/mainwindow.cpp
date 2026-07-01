#include "mainwindow.h"
#include "MemoryCache.h"
#include "RegionViewDialog.h"
#include "interface_generated.h"
#include "ui_mainwindow.h"
#include <QCloseEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QTime>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>
#include <windows.h>

namespace
{

constexpr int baseAddressRole = Qt::UserRole + 1;
constexpr int offsetRole = Qt::UserRole + 2;
constexpr int regionSizeRole = Qt::UserRole + 3;
constexpr int dataSizeRole = Qt::UserRole + 4;
constexpr int valueTypeRole = Qt::UserRole + 5;

constexpr std::array<std::pair<const char *, Interface::ValueType>, Interface::ValueType_LAST> valueTypes{
    {{"Int32", Interface::ValueType_Int32},
     {"Float", Interface::ValueType_Float},
     {"Double", Interface::ValueType_Double},
     {"Int64", Interface::ValueType_Int64},
     {"String", Interface::ValueType_String},
     {"String16", Interface::ValueType_String16},
     {"ByteArray", Interface::ValueType_ByteArray}}};

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

QString valueTypeName(int type)
{
    const auto index = static_cast<size_t>(type);
    return index < valueTypes.size() ? QString::fromUtf8(valueTypes[index].first) : QStringLiteral("Unknown");
}

QString memoryTypeName(uint32_t type)
{
    switch (type)
    {
    case MEM_PRIVATE:
        return QStringLiteral("Private");
    case MEM_MAPPED:
        return QStringLiteral("Mapped");
    case MEM_IMAGE:
        return QStringLiteral("Image");
    default:
        return QString("0x%1").arg(type, 0, 16);
    }
}

QString memoryProtectionName(uint32_t protection)
{
    QString value;
    switch (protection & 0xff)
    {
    case PAGE_NOACCESS:
        value = QStringLiteral("No access");
        break;
    case PAGE_READONLY:
        value = QStringLiteral("Read");
        break;
    case PAGE_READWRITE:
        value = QStringLiteral("Read/Write");
        break;
    case PAGE_WRITECOPY:
        value = QStringLiteral("Copy-on-write");
        break;
    case PAGE_EXECUTE:
        value = QStringLiteral("Execute");
        break;
    case PAGE_EXECUTE_READ:
        value = QStringLiteral("Execute/Read");
        break;
    case PAGE_EXECUTE_READWRITE:
        value = QStringLiteral("Execute/Read/Write");
        break;
    case PAGE_EXECUTE_WRITECOPY:
        value = QStringLiteral("Execute/Copy-on-write");
        break;
    default:
        value = QString("0x%1").arg(protection, 0, 16);
        break;
    }
    if (protection & PAGE_GUARD)
        value += QStringLiteral(" | Guard");
    return value;
}

QString updatedStatus()
{
    return MainWindow::tr("Updated %1").arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")));
}

void preserveOrSelectContextRow(QTableWidget *table, int row)
{
    if (row < 0 || table->selectionModel()->isRowSelected(row, QModelIndex{}))
        return;

    table->clearSelection();
    table->selectRow(row);
}

std::vector<int> selectedTableRows(QTableWidget *table)
{
    std::vector<int> rows;
    const auto indexes = table->selectionModel()->selectedRows();
    rows.reserve(static_cast<size_t>(indexes.size()));
    for (const auto &index : indexes)
        rows.push_back(index.row());
    std::ranges::sort(rows);
    return rows;
}

QString csvCell(QString value)
{
    value.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow), m_rpc_client(m_sender)
{
    m_sender.init(BUFFER_NAME_TX, true);
    m_reciver.init(BUFFER_NAME_RX, BUFFER_CAPACITY, true);
    ui->setupUi(this);
    ui->dumpButton->setEnabled(false);
    ui->resultsTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->resultsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(ui->resultsTable, &QTableWidget::customContextMenuRequested, this, &MainWindow::showResultsContextMenu);
    for (auto *table : {ui->valueWatchTable, ui->regionWatchTable})
    {
        table->setContextMenuPolicy(Qt::CustomContextMenu);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        table->horizontalHeader()->setStretchLastSection(true);
    }
    connect(ui->valueWatchTable, &QTableWidget::customContextMenuRequested, this,
            &MainWindow::showValueWatchContextMenu);
    connect(ui->regionWatchTable, &QTableWidget::customContextMenuRequested, this,
            &MainWindow::showRegionWatchContextMenu);
    connect(ui->regionWatchTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (m_hookReady && !m_unhookPending && m_injector.isHooked() && row >= 0 &&
            static_cast<size_t>(row) < m_regionWatches.size())
            viewRegion(m_regionWatches[static_cast<size_t>(row)].occurrence,
                       {m_regionWatches[static_cast<size_t>(row)].occurrence});
    });
    m_watchTimer = new QTimer(this);
    connect(m_watchTimer, &QTimer::timeout, this, &MainWindow::refreshWatches);
    connect(ui->watchRefreshButton, &QPushButton::clicked, this, &MainWindow::refreshWatches);
    connect(ui->watchAutoRefreshCheck, &QCheckBox::toggled, this, [this](bool enabled) {
        if (enabled)
        {
            m_watchTimer->start(ui->watchIntervalSpin->value());
            refreshWatches();
        }
        else
        {
            m_watchTimer->stop();
        }
    });
    connect(ui->watchIntervalSpin, &QSpinBox::valueChanged, this, [this](int interval) {
        if (m_watchTimer->isActive())
            m_watchTimer->start(interval);
    });
    for (const auto &[text, type] : valueTypes)
    {
        ui->valueTypeCombo->addItem(text, static_cast<int>(type));
    }
    m_recive_thread = std::jthread(&MainWindow::MsgConsumerThread, this);
    connect(ui->windowSelectorCombo, &WindowSelectorCombo::aboutToShowPopup, this,
            &MainWindow::on_windowSelectorOpened);
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

    m_rpc_client.clear_callbacks();
    const bool hookRemoved = !m_injector.isHooked() || m_injector.unhook();
    if (hookRemoved)
    {
        // This is only a fallback for non-standard destruction; normal window
        // closing waits for FINALIZE_ACK in completeUnhook().
        m_sender.send_command(0, Interface::CommandID_FINALIZE, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);
        m_sender.close();
    }
    else
    {
        // Do not signal the target worker when UnhookWindowsHookEx failed.
        // Unmap locally while leaving its owned DLL reference pinned.
        m_sender.release(false);
    }
    delete ui;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_closeApproved)
    {
        event->accept();
        return;
    }

    event->ignore();
    if (m_unhookPending || m_unhookFinalizing)
    {
        m_closePending = true;
        return;
    }

    if (!m_injector.isHooked())
    {
        event->accept();
        return;
    }

    if (m_closePending)
        return;

    m_closePending = true;

    if (!m_hookReady || m_hookStopAcknowledged)
    {
        finishClose();
        return;
    }

    m_unhookPending = true;
    ui->hookButton->setEnabled(false);

    const bool sent = m_rpc_client.send_rpc_cb(
        [this](const Interface::CommandEnvelope *msg) {
            if (!msg || msg->id() != Interface::CommandID_ACK)
            {
                m_unhookPending = false;
                m_closePending = false;
                ui->hookButton->setEnabled(true);
                qWarning() << "Hook did not acknowledge shutdown; window remains open";
                return;
            }

            m_hookStopAcknowledged = true;
            finishClose();
        },
        Interface::CommandID_STOP, Interface::Command::Command_NONE, Interface::CreateEmptyCommand);

    if (!sent)
    {
        m_unhookPending = false;
        m_closePending = false;
        ui->hookButton->setEnabled(true);
        qWarning() << "Failed to send hook shutdown command; window remains open";
    }
}

void MainWindow::finishClose()
{
    finishUnhook();
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
    try
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
                break;

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
                this, [this, msg_data]() { HandleMessage(GetCommandEnvelope(msg_data->data())); },
                Qt::QueuedConnection);
        }
    }
    catch (const std::exception &error)
    {
        qCritical() << "IPC receiver thread failed:" << error.what();
    }
    catch (...)
    {
        qCritical() << "IPC receiver thread failed with an unknown error";
    }
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
    case Interface::CommandID_REGION_READ_ACK:
        qDebug() << "[HandleMessage] Received CommandID_REGION_READ_ACK command";
        m_rpc_client.call_cb(msg->request_id(), msg);
        break;
    case Interface::CommandID_NACK:
        qDebug() << "[HandleMessage] Received CommandID_NACK command";
        m_rpc_client.call_cb(msg->request_id(), msg);
        break;
    case Interface::CommandID_READY:
        if (m_hooked && !m_unhookPending)
        {
            m_hookReady = true;
            ui->hookButton->setText("Unhook");
            ui->dumpButton->setEnabled(true);
            qDebug() << "Hook worker is ready";
        }
        break;
    case Interface::CommandID_FIND_ACK: {
        auto *ack = msg->body_as_FindAck();
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
        for (auto occur : *occurrences)
        {
            FoundOccurrences found{
                .baseAddress = occur->base_address(),
                .offset = occur->offset(),
                .region_size = occur->region_size(),
                .data_size = occur->data_size(),
                .type = occur->type(),
            };

            m_occur_storage.put(data, MemoryCache::View{found});
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
    std::unique_lock lck(m_hook_mutex);
    if (!m_injector.isHooked())
    {
        std::wstring windowName = ui->windowSelectorCombo->currentText().toStdWString();
        if (windowName.empty())
        {
            qDebug() << "Window name is empty " << windowName;
            return;
        }

        // Reopen and clear IPC before the hook can start its worker in the target process.
        m_rpc_client.clear_callbacks();
        m_sender.reset();
        m_reciver.reset();
        if (m_injector.hook(windowName))
        {
            DWORD pid = 0;
            GetWindowThreadProcessId(m_injector.getHWND(), &pid);
            qDebug() << std::format(L"HookInjected {} {} {}", windowName,
                                    reinterpret_cast<uintptr_t>(m_injector.getHWND()), pid);
            ui->hookButton->setText("Cancel Hook");
            m_hooked = true;
            m_hookReady = false;
            m_unhookPending = false;
            m_unhookFinalizing = false;
            m_finalizeAcknowledged = false;
            m_hookStopAcknowledged = false;
            m_hook_cv.notify_all();
            ui->dumpButton->setEnabled(false);
        }
        else
        {
            qDebug() << "Hook is not injected " << windowName;
            if (m_injector.isHooked())
            {
                m_hookStopAcknowledged = true;
                ui->hookButton->setText("Retry Cleanup");
            }
        }
    }
    else
    {
        if (m_unhookPending)
            return;

        if (!m_hookReady)
        {
            // The hook is installed, but its worker has not confirmed startup.
            // Closing IPC and removing the hook does not require a STOP round-trip.
            m_hookStopAcknowledged = true;
            lck.unlock();
            finishUnhook();
            return;
        }

        if (m_hookStopAcknowledged)
        {
            lck.unlock();
            finishUnhook();
            return;
        }

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
                m_hookStopAcknowledged = true;
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
    if (m_unhookFinalizing)
        return;

    m_unhookPending = true;
    ui->hookButton->setEnabled(false);
    {
        std::scoped_lock lock(m_hook_mutex);
        m_hooked = false;
        m_hookReady = false;
        m_hook_cv.notify_all();
    }

    if (!m_injector.unhook())
    {
        m_unhookPending = false;
        ui->hookButton->setText("Retry Unhook");
        ui->hookButton->setEnabled(true);
        qWarning() << "Failed to remove hook";
        m_closePending = false;
        return;
    }

    // Old requests will never receive replies after STOP.  Remove their
    // callbacks before registering the FINALIZE callback.
    m_rpc_client.clear_callbacks();
    m_pendingWatchRequests = 0;
    m_watchRefreshInProgress = false;
    m_watchRefreshQueued = false;
    ui->watchRefreshButton->setEnabled(true);

    m_unhookFinalizing = true;
    m_finalizeAcknowledged = false;
    const uint64_t attempt = ++m_unhookAttempt;
    const bool sent = m_rpc_client.send_rpc_cb(
        [this, attempt](const Interface::CommandEnvelope *msg) {
            if (!m_unhookFinalizing || attempt != m_unhookAttempt)
                return;
            if (!msg || msg->id() != Interface::CommandID_ACK)
            {
                qWarning() << "Hook did not acknowledge FINALIZE; retaining its safety pin if necessary";
                completeUnhook();
                return;
            }
            m_finalizeAcknowledged = true;
            waitForTargetUnload(attempt, 100);
        },
        Interface::CommandID_FINALIZE, Interface::Command::Command_NONE, Interface::CreateEmptyCommand);

    if (!sent)
    {
        qWarning() << "Failed to send FINALIZE; target DLL will remain pinned for safety";
        completeUnhook();
        return;
    }

    QTimer::singleShot(5000, this, [this, attempt]() {
        if (!m_unhookFinalizing || m_finalizeAcknowledged || attempt != m_unhookAttempt)
            return;
        qWarning() << "Timed out waiting for FINALIZE acknowledgement; target DLL may remain pinned";
        completeUnhook();
    });
}

void MainWindow::waitForTargetUnload(uint64_t attempt, int retriesRemaining)
{
    if (!m_unhookFinalizing || attempt != m_unhookAttempt)
        return;
    if (!m_injector.isTargetModuleLoaded())
    {
        completeUnhook();
        return;
    }
    if (retriesRemaining <= 0)
    {
        qWarning() << "Target DLL remained loaded after FINALIZE acknowledgement";
        completeUnhook();
        return;
    }
    QTimer::singleShot(20, this,
                       [this, attempt, retriesRemaining]() { waitForTargetUnload(attempt, retriesRemaining - 1); });
}

void MainWindow::completeUnhook()
{
    m_rpc_client.clear_callbacks();
    m_sender.close();
    m_reciver.close();

    m_unhookPending = false;
    m_unhookFinalizing = false;
    m_finalizeAcknowledged = false;
    m_hookStopAcknowledged = false;
    ui->dumpButton->setEnabled(false);
    ui->hookButton->setText("Inject Hook");
    ui->hookButton->setEnabled(true);
    qDebug() << "unhooked";

    if (m_closePending)
    {
        m_closeApproved = true;
        QMetaObject::invokeMethod(this, [this]() { close(); }, Qt::QueuedConnection);
    }
}

void MainWindow::on_dumpButton_clicked()
{
    if (m_unhookPending || !m_hookReady || !m_injector.isHooked())
        return;

    qDebug() << "Dump button clicked";
    using namespace Interface;

    m_rpc_client.send_rpc(Interface::CommandID::CommandID_DUMP, Interface::Command::Command_NONE, CreateEmptyCommand);
}

void MainWindow::on_firstScanButton_clicked()
{
    using namespace Interface;
    if (m_unhookPending || !m_hookReady || !m_injector.isHooked())
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

    auto create_find_cmd = [](flatbuffers::FlatBufferBuilder &builder, Interface::ValueType type,
                              const std::vector<uint8_t> &data) {
        auto vec = builder.CreateVector(data);
        return Interface::CreateFindCommand(builder, type, vec);
    };

    m_rpc_client.send_rpc(Interface::CommandID::CommandID_FIND, Interface::Command::Command_FindCommand,
                          create_find_cmd, static_cast<Interface::ValueType>(value_type), data);
    qDebug() << "on_firstScan_clicked " << ui->valueTypeCombo->currentData().toInt() << " " << text;
}

void MainWindow::on_nextScanButton_clicked()
{
    if (m_unhookPending || !m_hookReady || !m_injector.isHooked())
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
            Interface::CommandID_READ, Interface::Command::Command_ReadCommand, Interface::CreateReadCommand,
            range.base, static_cast<uint64_t>(range.size));

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
        const bool matches = view.type() == type && cached_data && cached_data->size() >= value.size() &&
                             std::ranges::equal(value, std::span<const uint8_t>(cached_data->data(), value.size()));

        if (!matches)
        {
            m_occur_storage.remove_view(view);
        }
    }
}

void MainWindow::on_pressKeyButton_clicked()
{
    if (m_unhookPending || !m_hookReady || !m_injector.isHooked())
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

void MainWindow::showResultsContextMenu(const QPoint &position)
{
    auto *clickedItem = ui->resultsTable->itemAt(position);
    const int clickedRow = clickedItem ? clickedItem->row() : -1;
    preserveOrSelectContextRow(ui->resultsTable, clickedRow);

    const auto rows = selectedTableRows(ui->resultsTable);
    std::vector<FoundOccurrences> selectedOccurrences;
    selectedOccurrences.reserve(rows.size());
    for (const int row : rows)
    {
        auto *valueItem = ui->resultsTable->item(row, 0);
        if (!valueItem)
            continue;
        selectedOccurrences.push_back({
            .baseAddress = valueItem->data(baseAddressRole).toULongLong(),
            .offset = valueItem->data(offsetRole).toULongLong(),
            .region_size = valueItem->data(regionSizeRole).toULongLong(),
            .data_size = valueItem->data(dataSizeRole).toULongLong(),
            .type = valueItem->data(valueTypeRole).toInt(),
        });
    }

    QMenu menu(this);
    QAction *viewRegionAction = menu.addAction(tr("View region"));
    QAction *addValueWatchAction =
        menu.addAction(selectedOccurrences.size() > 1 ? tr("Add selected values to watch") : tr("Add value to watch"));
    QAction *addRegionWatchAction = menu.addAction(selectedOccurrences.size() > 1 ? tr("Add selected regions to watch")
                                                                                  : tr("Add region to watch"));
    menu.addSeparator();
    QAction *saveAction = menu.addAction(tr("Save table as CSV..."));
    viewRegionAction->setEnabled(clickedRow >= 0 && m_hookReady && !m_unhookPending && m_injector.isHooked());
    addValueWatchAction->setEnabled(!selectedOccurrences.empty());
    addRegionWatchAction->setEnabled(!selectedOccurrences.empty());
    QAction *selectedAction = menu.exec(ui->resultsTable->viewport()->mapToGlobal(position));
    if (selectedAction == addValueWatchAction)
    {
        bool added = false;
        for (const auto &occurrence : selectedOccurrences)
            added = addValueWatch(occurrence, false) || added;
        if (added)
            refreshWatches();
    }
    else if (selectedAction == addRegionWatchAction)
    {
        bool added = false;
        for (const auto &occurrence : selectedOccurrences)
            added = addRegionWatch(occurrence, false) || added;
        if (added)
            refreshWatches();
    }
    else if (selectedAction == viewRegionAction)
    {
        auto *valueItem = ui->resultsTable->item(clickedRow, 0);
        if (!valueItem)
            return;
        FoundOccurrences occurrence{
            .baseAddress = valueItem->data(baseAddressRole).toULongLong(),
            .offset = valueItem->data(offsetRole).toULongLong(),
            .region_size = valueItem->data(regionSizeRole).toULongLong(),
            .data_size = valueItem->data(dataSizeRole).toULongLong(),
            .type = valueItem->data(valueTypeRole).toInt(),
        };
        std::vector<FoundOccurrences> regionOccurrences;
        const uint64_t regionBase = occurrence.baseAddress;
        const uint64_t regionSize = occurrence.region_size;
        for (const auto &view : m_occur_storage.views())
        {
            const auto &candidate = view.occurrence;
            const uint64_t address = candidate.baseAddress + candidate.offset;
            if (address < candidate.baseAddress || address < regionBase)
                continue;

            const uint64_t relativeOffset = address - regionBase;
            if (relativeOffset >= regionSize || candidate.data_size > regionSize - relativeOffset)
                continue;

            FoundOccurrences normalized = candidate;
            normalized.baseAddress = regionBase;
            normalized.offset = relativeOffset;
            normalized.region_size = regionSize;
            regionOccurrences.push_back(normalized);
        }
        if (regionOccurrences.empty())
            regionOccurrences.push_back(occurrence);
        viewRegion(occurrence, std::move(regionOccurrences));
    }
    else if (selectedAction == saveAction)
    {
        saveTable(ui->resultsTable, tr("scan results"));
    }
}

bool MainWindow::addValueWatch(const FoundOccurrences &occurrence, bool refresh)
{
    const uint64_t address = occurrence.baseAddress + occurrence.offset;
    const auto duplicate = std::ranges::find_if(m_valueWatches, [address](const FoundOccurrences &entry) {
        return entry.baseAddress + entry.offset == address;
    });
    if (duplicate != m_valueWatches.end())
    {
        ui->valueWatchTable->selectRow(static_cast<int>(std::distance(m_valueWatches.begin(), duplicate)));
        return false;
    }

    m_valueWatches.push_back(occurrence);
    updateValueWatchRow(m_valueWatches.size() - 1, QStringLiteral("-"), tr("Pending"));
    if (refresh)
        refreshWatches();
    return true;
}

bool MainWindow::addRegionWatch(const FoundOccurrences &occurrence, bool refresh)
{
    const auto duplicate = std::ranges::find_if(m_regionWatches, [&occurrence](const RegionWatch &entry) {
        return entry.occurrence.baseAddress == occurrence.baseAddress;
    });
    if (duplicate != m_regionWatches.end())
    {
        ui->regionWatchTable->selectRow(static_cast<int>(std::distance(m_regionWatches.begin(), duplicate)));
        return false;
    }

    m_regionWatches.push_back(RegionWatch{occurrence, {}});
    updateRegionWatchRow(m_regionWatches.size() - 1, nullptr, tr("Pending"));
    if (refresh)
        refreshWatches();
    return true;
}

void MainWindow::showValueWatchContextMenu(const QPoint &position)
{
    auto *item = ui->valueWatchTable->itemAt(position);
    const int clickedRow = item ? item->row() : -1;
    preserveOrSelectContextRow(ui->valueWatchTable, clickedRow);
    auto rows = selectedTableRows(ui->valueWatchTable);

    QMenu menu(this);
    QAction *viewAction = menu.addAction(tr("View region"));
    QAction *addRegionWatchAction =
        menu.addAction(rows.size() > 1 ? tr("Add selected regions to watch") : tr("Add region to watch"));
    QAction *removeAction =
        menu.addAction(rows.size() > 1 ? tr("Remove selected from watch") : tr("Remove from watch"));
    viewAction->setEnabled(clickedRow >= 0 && static_cast<size_t>(clickedRow) < m_valueWatches.size() && m_hookReady &&
                           !m_unhookPending && m_injector.isHooked());
    addRegionWatchAction->setEnabled(!rows.empty());
    removeAction->setEnabled(!rows.empty());
    menu.addSeparator();
    QAction *saveAction = menu.addAction(tr("Save table as CSV..."));
    QAction *selectedAction = menu.exec(ui->valueWatchTable->viewport()->mapToGlobal(position));
    if (selectedAction == viewAction)
    {
        const auto occurrence = m_valueWatches[static_cast<size_t>(clickedRow)];
        std::vector<FoundOccurrences> regionOccurrences;
        for (const auto &candidate : m_valueWatches)
        {
            const uint64_t address = candidate.baseAddress + candidate.offset;
            if (address < candidate.baseAddress || address < occurrence.baseAddress)
                continue;

            const uint64_t relativeOffset = address - occurrence.baseAddress;
            if (relativeOffset >= occurrence.region_size ||
                candidate.data_size > occurrence.region_size - relativeOffset)
                continue;

            FoundOccurrences normalized = candidate;
            normalized.baseAddress = occurrence.baseAddress;
            normalized.offset = relativeOffset;
            normalized.region_size = occurrence.region_size;
            regionOccurrences.push_back(normalized);
        }
        if (regionOccurrences.empty())
            regionOccurrences.push_back(occurrence);
        viewRegion(occurrence, std::move(regionOccurrences));
    }
    else if (selectedAction == addRegionWatchAction)
    {
        bool added = false;
        for (const int row : rows)
        {
            if (row < 0 || static_cast<size_t>(row) >= m_valueWatches.size())
                continue;
            added = addRegionWatch(m_valueWatches[static_cast<size_t>(row)], false) || added;
        }
        if (added)
            refreshWatches();
    }
    else if (selectedAction == saveAction)
    {
        saveTable(ui->valueWatchTable, tr("value watch"));
    }
    else if (selectedAction == removeAction)
    {
        std::ranges::sort(rows, std::greater{});
        for (const int row : rows)
        {
            if (row < 0 || static_cast<size_t>(row) >= m_valueWatches.size())
                continue;
            m_valueWatches.erase(m_valueWatches.begin() + row);
            ui->valueWatchTable->removeRow(row);
        }
    }
}

void MainWindow::showRegionWatchContextMenu(const QPoint &position)
{
    auto *item = ui->regionWatchTable->itemAt(position);
    const int clickedRow = item ? item->row() : -1;
    preserveOrSelectContextRow(ui->regionWatchTable, clickedRow);
    auto rows = selectedTableRows(ui->regionWatchTable);

    QMenu menu(this);
    QAction *viewAction = menu.addAction(tr("View region"));
    QAction *removeAction =
        menu.addAction(rows.size() > 1 ? tr("Remove selected from watch") : tr("Remove from watch"));
    menu.addSeparator();
    QAction *saveAction = menu.addAction(tr("Save table as CSV..."));
    viewAction->setEnabled(clickedRow >= 0 && static_cast<size_t>(clickedRow) < m_regionWatches.size() && m_hookReady &&
                           !m_unhookPending && m_injector.isHooked());
    removeAction->setEnabled(!rows.empty());
    QAction *selectedAction = menu.exec(ui->regionWatchTable->viewport()->mapToGlobal(position));
    if (selectedAction == viewAction)
    {
        const auto occurrence = m_regionWatches[static_cast<size_t>(clickedRow)].occurrence;
        viewRegion(occurrence, {occurrence});
    }
    else if (selectedAction == removeAction)
    {
        std::ranges::sort(rows, std::greater{});
        for (const int row : rows)
        {
            if (row < 0 || static_cast<size_t>(row) >= m_regionWatches.size())
                continue;
            m_regionWatches.erase(m_regionWatches.begin() + row);
            ui->regionWatchTable->removeRow(row);
        }
    }
    else if (selectedAction == saveAction)
    {
        saveTable(ui->regionWatchTable, tr("region watch"));
    }
}

void MainWindow::saveTable(QTableWidget *table, const QString &title)
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save %1").arg(title), title + QStringLiteral(".csv"),
                                                    tr("CSV files (*.csv);;All files (*)"));
    if (fileName.isEmpty())
        return;
    if (!fileName.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        fileName += QStringLiteral(".csv");

    QSaveFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(this, tr("Save table"),
                             tr("Could not open the file for writing:\n%1").arg(file.errorString()));
        return;
    }

    QTextStream stream(&file);
    stream << QChar(0xfeff);
    for (int column = 0; column < table->columnCount(); ++column)
    {
        if (column != 0)
            stream << ',';
        const auto *header = table->horizontalHeaderItem(column);
        stream << csvCell(header ? header->text() : QString{});
    }
    stream << '\n';

    for (int row = 0; row < table->rowCount(); ++row)
    {
        for (int column = 0; column < table->columnCount(); ++column)
        {
            if (column != 0)
                stream << ',';
            const auto *item = table->item(row, column);
            stream << csvCell(item ? item->text() : QString{});
        }
        stream << '\n';
    }

    stream.flush();
    if (stream.status() != QTextStream::Ok)
    {
        QMessageBox::warning(this, tr("Save table"), tr("Could not save the table:\n%1").arg(file.errorString()));
        return;
    }

    if (!file.commit())
        QMessageBox::warning(this, tr("Save table"), tr("Could not save the table:\n%1").arg(file.errorString()));
}

void MainWindow::updateValueWatchRow(size_t index, const QString &value, const QString &status)
{
    if (index >= m_valueWatches.size())
        return;

    auto *table = ui->valueWatchTable;
    if (table->rowCount() <= static_cast<int>(index))
        table->setRowCount(static_cast<int>(index + 1));

    const auto &occurrence = m_valueWatches[index];
    const int row = static_cast<int>(index);
    table->setItem(row, 0, new QTableWidgetItem(value));
    table->setItem(row, 1,
                   new QTableWidgetItem(QString("0x%1").arg(occurrence.baseAddress + occurrence.offset, 0, 16)));
    table->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(occurrence.baseAddress, 0, 16)));
    table->setItem(row, 3, new QTableWidgetItem(QString::number(occurrence.offset)));
    table->setItem(row, 4, new QTableWidgetItem(QString::number(occurrence.region_size)));
    table->setItem(row, 5, new QTableWidgetItem(QString::number(occurrence.data_size)));
    table->setItem(row, 6, new QTableWidgetItem(valueTypeName(occurrence.type)));
    table->setItem(row, 7, new QTableWidgetItem{});
    table->setItem(row, 8, new QTableWidgetItem(status));
}

void MainWindow::updateRegionWatchRow(size_t index, const MemoryRegionDetails *details, const QString &status)
{
    if (index >= m_regionWatches.size())
        return;

    auto &watch = m_regionWatches[index];
    if (details)
        watch.details = *details;

    auto *table = ui->regionWatchTable;
    if (table->rowCount() <= static_cast<int>(index))
        table->setRowCount(static_cast<int>(index + 1));

    const int row = static_cast<int>(index);
    const uint64_t base = watch.details.available ? watch.details.baseAddress : watch.occurrence.baseAddress;
    const uint64_t size = watch.details.available ? watch.details.regionSize : watch.occurrence.region_size;
    table->setItem(row, 0, new QTableWidgetItem(QString("0x%1").arg(base, 0, 16)));
    table->setItem(row, 1, new QTableWidgetItem(QString::number(size)));
    table->setItem(row, 2,
                   new QTableWidgetItem(watch.details.available ? memoryProtectionName(watch.details.protect)
                                                                : QStringLiteral("-")));
    table->setItem(
        row, 3,
        new QTableWidgetItem(watch.details.available ? memoryTypeName(watch.details.type) : QStringLiteral("-")));
    table->setItem(row, 4,
                   new QTableWidgetItem(watch.details.available && !watch.details.moduleName.isEmpty()
                                            ? watch.details.moduleName
                                            : QStringLiteral("-")));
    table->setItem(row, 5, new QTableWidgetItem(status));
}

void MainWindow::refreshWatches()
{
    if (m_watchRefreshInProgress)
    {
        m_watchRefreshQueued = true;
        return;
    }

    if (!m_hookReady || m_unhookPending || !m_injector.isHooked())
    {
        for (size_t i = 0; i < m_valueWatches.size(); ++i)
            updateValueWatchRow(i,
                                ui->valueWatchTable->item(static_cast<int>(i), 0)
                                    ? ui->valueWatchTable->item(static_cast<int>(i), 0)->text()
                                    : QStringLiteral("-"),
                                tr("Disconnected"));
        for (size_t i = 0; i < m_regionWatches.size(); ++i)
            updateRegionWatchRow(i, nullptr, tr("Disconnected"));
        return;
    }

    m_pendingWatchRequests = m_valueWatches.size() + m_regionWatches.size();
    if (m_pendingWatchRequests == 0)
        return;

    m_watchRefreshInProgress = true;
    m_watchRefreshQueued = false;
    ui->watchRefreshButton->setEnabled(false);

    const auto valueWatches = m_valueWatches;
    for (const auto &occurrence : valueWatches)
    {
        const uint64_t address = occurrence.baseAddress + occurrence.offset;
        const bool validSize = occurrence.data_size != 0 && occurrence.data_size <= MAX_READ_SIZE;
        const bool sent =
            validSize &&
            m_rpc_client.send_rpc_cb(
                [this, occurrence, address](const Interface::CommandEnvelope *msg) {
                    const auto current = std::ranges::find_if(m_valueWatches, [address](const FoundOccurrences &entry) {
                        return entry.baseAddress + entry.offset == address;
                    });
                    if (current != m_valueWatches.end())
                    {
                        const size_t index = static_cast<size_t>(std::distance(m_valueWatches.begin(), current));
                        const auto *ack =
                            msg && msg->id() == Interface::CommandID_READ_ACK ? msg->body_as_ReadAck() : nullptr;
                        const auto *data = ack ? ack->data() : nullptr;
                        if (data && data->size() == occurrence.data_size)
                        {
                            const std::span<const uint8_t> bytes(data->data(), data->size());
                            updateValueWatchRow(index,
                                                QString::fromStdString(valueToString(
                                                    bytes, static_cast<Interface::ValueType>(occurrence.type))),
                                                updatedStatus());
                        }
                        else
                        {
                            const QString oldValue = ui->valueWatchTable->item(static_cast<int>(index), 0)
                                                         ? ui->valueWatchTable->item(static_cast<int>(index), 0)->text()
                                                         : QStringLiteral("-");
                            updateValueWatchRow(index, oldValue, tr("Read failed"));
                        }
                    }
                    finishWatchRequest();
                },
                Interface::CommandID_READ, Interface::Command::Command_ReadCommand, Interface::CreateReadCommand,
                address, occurrence.data_size);
        if (!sent)
        {
            const auto current = std::ranges::find_if(m_valueWatches, [address](const FoundOccurrences &entry) {
                return entry.baseAddress + entry.offset == address;
            });
            if (current != m_valueWatches.end())
            {
                const size_t index = static_cast<size_t>(std::distance(m_valueWatches.begin(), current));
                const QString oldValue = ui->valueWatchTable->item(static_cast<int>(index), 0)
                                             ? ui->valueWatchTable->item(static_cast<int>(index), 0)->text()
                                             : QStringLiteral("-");
                updateValueWatchRow(index, oldValue, validSize ? tr("Send failed") : tr("Invalid size"));
            }
            finishWatchRequest();
        }
    }

    const auto regionWatches = m_regionWatches;
    for (const auto &watch : regionWatches)
    {
        const uint64_t base = watch.occurrence.baseAddress;
        const bool sent = m_rpc_client.send_rpc_cb(
            [this, base](const Interface::CommandEnvelope *msg) {
                const auto current = std::ranges::find_if(
                    m_regionWatches, [base](const RegionWatch &entry) { return entry.occurrence.baseAddress == base; });
                if (current != m_regionWatches.end())
                {
                    const size_t index = static_cast<size_t>(std::distance(m_regionWatches.begin(), current));
                    const auto *ack = msg && msg->id() == Interface::CommandID_REGION_READ_ACK
                                          ? msg->body_as_RegionReadAck()
                                          : nullptr;
                    const auto *region = ack ? ack->region() : nullptr;
                    if (region)
                    {
                        const auto details = MemoryRegionDetails::fromFlatbuffer(region);
                        updateRegionWatchRow(index, &details, updatedStatus());
                    }
                    else
                    {
                        updateRegionWatchRow(index, nullptr, tr("Read failed"));
                    }
                }
                finishWatchRequest();
            },
            Interface::CommandID_REGION_READ, Interface::Command::Command_RegionReadCommand,
            Interface::CreateRegionReadCommand, base, uint64_t{0}, base);
        if (!sent)
        {
            const auto current = std::ranges::find_if(
                m_regionWatches, [base](const RegionWatch &entry) { return entry.occurrence.baseAddress == base; });
            if (current != m_regionWatches.end())
                updateRegionWatchRow(static_cast<size_t>(std::distance(m_regionWatches.begin(), current)), nullptr,
                                     tr("Send failed"));
            finishWatchRequest();
        }
    }
}

void MainWindow::finishWatchRequest()
{
    if (m_pendingWatchRequests != 0)
        --m_pendingWatchRequests;
    if (m_pendingWatchRequests == 0)
    {
        m_watchRefreshInProgress = false;
        ui->watchRefreshButton->setEnabled(true);
        if (m_watchRefreshQueued)
            QTimer::singleShot(0, this, &MainWindow::refreshWatches);
    }
}

void MainWindow::viewRegion(const FoundOccurrences &occurrence, std::vector<FoundOccurrences> regionOccurrences)
{
    requestRegionData(occurrence, [this, occurrence, regionOccurrences = std::move(regionOccurrences)](
                                      std::vector<uint8_t> data, MemoryRegionDetails details) mutable {
        auto *dialog = new RegionViewDialog(occurrence, std::move(regionOccurrences), std::move(data),
                                            std::move(details), ui->windowSelectorCombo->currentText(), this);
        QPointer<RegionViewDialog> guardedDialog(dialog);
        connect(dialog, &RegionViewDialog::updateRequested, this, [this, guardedDialog, occurrence]() {
            if (!guardedDialog)
                return;

            guardedDialog->setUpdating(true);
            auto finishUpdate = [guardedDialog]() {
                if (guardedDialog)
                    guardedDialog->setUpdating(false);
            };
            requestRegionData(
                occurrence,
                [guardedDialog](std::vector<uint8_t> refreshedData, MemoryRegionDetails refreshedDetails) {
                    if (!guardedDialog)
                        return;
                    guardedDialog->setRegionData(std::move(refreshedData), std::move(refreshedDetails));
                    guardedDialog->setUpdating(false);
                },
                std::move(finishUpdate));
        });
        dialog->show();
    });
}

bool MainWindow::requestRegionData(const FoundOccurrences &occurrence, RegionDataCallback done,
                                   std::function<void()> failed)
{
    if (occurrence.region_size == 0 || occurrence.region_size > MAX_REGION_READ_SIZE)
    {
        QMessageBox::warning(this, tr("View region"),
                             tr("Region size %1 cannot be read in one IPC message.").arg(occurrence.region_size));
        if (failed)
            failed();
        return false;
    }

    const bool sent = m_rpc_client.send_rpc_cb(
        [this, done = std::move(done), failed](const Interface::CommandEnvelope *msg) mutable {
            if (!msg || msg->id() != Interface::CommandID_REGION_READ_ACK)
            {
                QMessageBox::warning(this, tr("View region"), tr("Failed to read the selected region."));
                if (failed)
                    failed();
                return;
            }

            const auto *ack = msg->body_as_RegionReadAck();
            const auto *readData = ack ? ack->data() : nullptr;
            if (!readData)
            {
                QMessageBox::warning(this, tr("View region"), tr("The selected region returned no data."));
                if (failed)
                    failed();
                return;
            }

            std::vector<uint8_t> data;
            if (readData->size() != 0)
                data.assign(readData->data(), readData->data() + readData->size());

            auto details = MemoryRegionDetails::fromFlatbuffer(ack->region());

            done(std::move(data), std::move(details));
        },
        Interface::CommandID_REGION_READ, Interface::Command::Command_RegionReadCommand,
        Interface::CreateRegionReadCommand, occurrence.baseAddress, occurrence.region_size,
        occurrence.baseAddress + occurrence.offset);

    if (!sent)
    {
        QMessageBox::warning(this, tr("View region"), tr("Failed to send the region read request."));
        if (failed)
            failed();
    }
    return sent;
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

        const auto &occur = view.occurrence;
        const auto value_type = view.type();
        const auto type_index = static_cast<size_t>(value_type);
        const char *type_text = type_index < valueTypes.size() ? valueTypes[type_index].first : "Unknown";
        const auto value_text = valueToString(*data, value_type);
        auto *valueItem = new QTableWidgetItem(QString::fromStdString(value_text));
        valueItem->setData(baseAddressRole, QVariant::fromValue<qulonglong>(occur.baseAddress));
        valueItem->setData(offsetRole, QVariant::fromValue<qulonglong>(occur.offset));
        valueItem->setData(regionSizeRole, QVariant::fromValue<qulonglong>(occur.region_size));
        valueItem->setData(dataSizeRole, QVariant::fromValue<qulonglong>(occur.data_size));
        valueItem->setData(valueTypeRole, occur.type);
        table->setItem(row, 0, valueItem);
        table->setItem(row, 1, new QTableWidgetItem(QString("0x%1").arg(occur.baseAddress + occur.offset, 0, 16)));
        table->setItem(row, 2, new QTableWidgetItem(QString("0x%1").arg(occur.baseAddress, 0, 16)));
        table->setItem(row, 3, new QTableWidgetItem(QString::number(occur.offset)));
        table->setItem(row, 4, new QTableWidgetItem(QString::number(occur.region_size)));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(occur.data_size)));
        table->setItem(row, 6, new QTableWidgetItem(QString::fromUtf8(type_text)));
        table->setItem(row, 7, new QTableWidgetItem{});
        ++row;
    }

    table->setRowCount(row);
    table->setSortingEnabled(true);
}
