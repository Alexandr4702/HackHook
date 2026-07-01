#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <exception>
#include <format>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <psapi.h>

#include "MemDumper.h"
#include "MemoryScanner.h"
#include "MyHook.h"
#include "interface_generated.h"

void SendKeyToWindow(HWND hWnd, char key);
extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) noexcept;

namespace
{
struct RegionMetadata
{
    bool available = false;
    uint64_t address = 0;
    uint64_t baseAddress = 0;
    uint64_t allocationBase = 0;
    uint32_t allocationProtect = 0;
    uint64_t regionSize = 0;
    uint32_t state = 0;
    uint32_t protect = 0;
    uint32_t type = 0;
    uint64_t moduleBase = 0;
    uint64_t moduleSize = 0;
    std::string moduleName;
    std::string modulePath;
    std::string mappedPath;
    bool isHeap = false;
    uint64_t heapHandle = 0;
    uint64_t heapBlock = 0;
    uint64_t heapBlockSize = 0;
    uint32_t heapFlags = 0;
    bool workingSetQueried = false;
    bool workingSetValid = false;
    uint32_t workingSetProtect = 0;
    uint32_t numaNode = 0;
    uint32_t shareCount = 0;
    bool shared = false;
    bool locked = false;
    bool largePage = false;
    bool bad = false;
};

std::string utf8(std::wstring_view text)
{
    if (text.empty())
        return {};

    const int size =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

RegionMetadata query_region_metadata(uintptr_t address)
{
    RegionMetadata result;
    result.address = address;

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<const void *>(address), &mbi, sizeof(mbi)) != sizeof(mbi))
        return result;

    result.available = true;
    result.baseAddress = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    result.allocationBase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
    result.allocationProtect = mbi.AllocationProtect;
    result.regionSize = mbi.RegionSize;
    result.state = mbi.State;
    result.protect = mbi.Protect;
    result.type = mbi.Type;

    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module))
    {
        MODULEINFO moduleInfo{};
        if (GetModuleInformation(GetCurrentProcess(), module, &moduleInfo, sizeof(moduleInfo)))
        {
            result.moduleBase = reinterpret_cast<uintptr_t>(moduleInfo.lpBaseOfDll);
            result.moduleSize = moduleInfo.SizeOfImage;
        }

        std::array<wchar_t, 32768> path{};
        const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length != 0 && length < path.size())
        {
            std::wstring_view fullPath(path.data(), length);
            result.modulePath = utf8(fullPath);
            const size_t separator = fullPath.find_last_of(L"\\/");
            result.moduleName = utf8(fullPath.substr(separator == std::wstring_view::npos ? 0 : separator + 1));
        }
    }

    std::array<wchar_t, 32768> mappedPath{};
    const DWORD mappedLength = GetMappedFileNameW(GetCurrentProcess(), reinterpret_cast<void *>(address),
                                                  mappedPath.data(), static_cast<DWORD>(mappedPath.size()));
    if (mappedLength != 0 && mappedLength < mappedPath.size())
        result.mappedPath = utf8(std::wstring_view(mappedPath.data(), mappedLength));

    const DWORD heapCount = mbi.Type == MEM_PRIVATE ? GetProcessHeaps(0, nullptr) : 0;
    if (heapCount != 0)
    {
        std::vector<HANDLE> heaps(heapCount);
        const DWORD returned = GetProcessHeaps(heapCount, heaps.data());
        for (DWORD i = 0; i < std::min(heapCount, returned) && !result.isHeap; ++i)
        {
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(heaps[i], &entry))
            {
                const uintptr_t block = reinterpret_cast<uintptr_t>(entry.lpData);
                if (address >= block && address - block < entry.cbData)
                {
                    result.isHeap = true;
                    result.heapHandle = reinterpret_cast<uintptr_t>(heaps[i]);
                    result.heapBlock = block;
                    result.heapBlockSize = entry.cbData;
                    result.heapFlags = entry.wFlags;
                    break;
                }
            }
        }
    }

    PSAPI_WORKING_SET_EX_INFORMATION workingSet{};
    workingSet.VirtualAddress = reinterpret_cast<void *>(address);
    if (QueryWorkingSetEx(GetCurrentProcess(), &workingSet, sizeof(workingSet)))
    {
        result.workingSetQueried = true;
        result.workingSetValid = workingSet.VirtualAttributes.Valid;
        result.shared = workingSet.VirtualAttributes.Shared;
        result.bad = workingSet.VirtualAttributes.Bad;
        if (result.workingSetValid)
        {
            result.workingSetProtect = workingSet.VirtualAttributes.Win32Protection;
            result.numaNode = workingSet.VirtualAttributes.Node;
            result.shareCount = workingSet.VirtualAttributes.ShareCount;
            result.locked = workingSet.VirtualAttributes.Locked;
            result.largePage = workingSet.VirtualAttributes.LargePage;
        }
    }

    return result;
}

void report_boundary_error(Logger *logger, const char *boundary, const char *details = nullptr) noexcept
{
    if (logger)
        logger->emergency(boundary, details);
    else
        Logger::emergency_to_default(boundary, details);
}

std::string output_root()
{
    std::array<char, MAX_PATH> path{};
    const DWORD length = GetTempPathA(static_cast<DWORD>(path.size()), path.data());

    std::string root;
    if (length != 0 && length < path.size())
        root.assign(path.data(), length);
    else
        root = ".\\";

    if (!root.empty() && root.back() != '\\' && root.back() != '/')
        root.push_back('\\');
    root += "HackHook\\";

    CreateDirectoryA(root.c_str(), nullptr);

    return root;
}
} // namespace

MyHook &MyHook::getInstance()
{
    alignas(MyHook) static std::byte storage[sizeof(MyHook)];
    static MyHook *instance = std::construct_at(reinterpret_cast<MyHook *>(storage));
    return *instance;
}

MyHook::MyHook()
    : allocate_size(128 * 1024 * 1024),
      m_pmrPoolMem(VirtualAlloc(nullptr, allocate_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)),
      m_monotonicPool(m_pmrPoolMem, allocate_size), m_pool(&m_monotonicPool)
{
}

MyHook::~MyHook() noexcept
{
    shutdown();
}

void MyHook::shutdown() noexcept
{
    if (m_shutdown.exchange(true, std::memory_order_acq_rel))
        return;

    try
    {
        m_memTool.reset();
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::shutdown MemTool");
    }
    try
    {
        m_reciver.release();
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::shutdown receiver");
    }
    try
    {
        m_sender.release(!m_stopAcknowledged.load(std::memory_order_acquire));
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::shutdown sender");
    }

    try
    {
        m_pool.release();
        m_monotonicPool.release();
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::shutdown pools");
    }

    if (m_pmrPoolMem)
    {
        VirtualFree(m_pmrPoolMem, 0, MEM_RELEASE);
        m_pmrPoolMem = nullptr;
    }

    std::string{}.swap(g_params.logDumpLocation);
    try
    {
        LOG(m_log, "MyHook stopped and resources released");
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::shutdown log");
    }
    m_log.close();
}

void MyHook::start() noexcept
{
    State expected = State::Stopped;
    if (!m_state.compare_exchange_strong(expected, State::Starting, std::memory_order_acq_rel))
        return;

    HMODULE module = nullptr;
    auto cleanup_failed_start = [this, &module]() noexcept {
        m_state.store(State::Stopping, std::memory_order_release);
        m_running.store(false, std::memory_order_release);
        shutdown();

        HMODULE owned_module = std::exchange(module, nullptr);
        if (owned_module)
            FreeLibrary(owned_module);
    };

    try
    {
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCSTR>(&HookProc), &module))
        {
            cleanup_failed_start();
            return;
        }

        m_memTool.emplace();
        m_reciver.init(BUFFER_NAME_TX, BUFFER_CAPACITY, false);
        m_sender.init(BUFFER_NAME_RX, false);
        m_running.store(true, std::memory_order_release);

        g_params.logDumpLocation = output_root() + "dump_" + Logger::GetTimestamp() + "\\";
        CreateDirectoryA(g_params.logDumpLocation.c_str(), nullptr);
        m_log.init(g_params.logDumpLocation + "log.txt");

        auto context = std::make_unique<WorkerContext>(WorkerContext{this, module});
        WorkerContext *raw_context = context.release();
        m_state.store(State::Running, std::memory_order_release);
        HANDLE worker = CreateThread(nullptr, 0, &MyHook::ThreadWrapperMsg, raw_context, 0, nullptr);
        if (!worker)
        {
            context.reset(raw_context);
            cleanup_failed_start();
            return;
        }

        module = nullptr; // WorkerContext now owns this module reference.
        CloseHandle(worker);
    }
    catch (const std::exception &error)
    {
        report_boundary_error(&m_log, "MyHook::start", error.what());
        cleanup_failed_start();
    }
    catch (...)
    {
        report_boundary_error(&m_log, "MyHook::start");
        cleanup_failed_start();
    }
}

void MyHook::stop()
{
    State expected = State::Running;
    if (!m_state.compare_exchange_strong(expected, State::Stopping, std::memory_order_acq_rel))
        return;

    m_running.store(false, std::memory_order_release);
    m_reciver.close();
    m_sender.close();
    LOG(m_log, "MyHook Stopped");
}

DWORD WINAPI MyHook::ThreadWrapperMsg(LPVOID param) noexcept
{
    std::unique_ptr<WorkerContext> context(static_cast<WorkerContext *>(param));
    if (!context)
        return ERROR_INVALID_PARAMETER;

    auto *self = context->hook;
    HMODULE module = context->module;
    context.reset();

    DWORD result = ERROR_SUCCESS;
    try
    {
        result = self->MsgConsumerThread();
    }
    catch (const std::exception &error)
    {
        report_boundary_error(&self->m_log, "ThreadWrapperMsg", error.what());
        result = ERROR_UNHANDLED_EXCEPTION;
    }
    catch (...)
    {
        report_boundary_error(&self->m_log, "ThreadWrapperMsg");
        result = ERROR_UNHANDLED_EXCEPTION;
    }

    self->m_running.store(false, std::memory_order_release);
    self->m_state.store(State::Stopping, std::memory_order_release);
    self->shutdown();

    if (module && self->m_finalizeReceived.load(std::memory_order_acquire))
        FreeLibraryAndExitThread(module, result);

    // On an IPC/protocol failure there is no proof that the Windows hook was
    // removed.  Intentionally retain the owned module reference instead of
    // risking an unload underneath HookProc.

    return result;
}

void MyHook::HandleMessage(const Interface::CommandEnvelope *msg)
{
    if (msg == nullptr)
        return;

    switch (msg->id())
    {
    case Interface::CommandID_WRITE: {
        const auto *cmd = msg->body_as_WriteCommand();
        if (!cmd)
        {
            LOG(m_log, "WRITE command: invalid payload");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }

        const auto offset = cmd->offset();
        const auto data = cmd->data();
        if (!data)
        {
            LOG(m_log, "WRITE command: missing data");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }
        const auto size = data->size();

        LOG(m_log, std::format("Received write command with offset: {}, size: {}", offset, size));

        if (!m_memTool)
        {
            LOG(m_log, "m_memTool is not created");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }

        const bool ok = m_memTool->write(offset, data->data(), size);

        if (!ok)
            LOG(m_log, "WRITE failed");

        const auto response = ok ? Interface::CommandID::CommandID_ACK : Interface::CommandID::CommandID_NACK;
        m_sender.send_command(msg->request_id(), response, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);

        break;
    }
    case Interface::CommandID_READ: {
        const auto *cmd = msg->body_as_ReadCommand();
        if (!cmd || cmd->size() > MAX_READ_SIZE)
        {
            LOG(m_log, "READ command: invalid payload or size");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }
        LOG(m_log, std::format("Received read command with offset: {}", cmd->offset()));
        if (!m_memTool)
        {
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }

        std::pmr::vector<uint8_t> readed_data(cmd->size(), &m_pool);
        auto readed = m_memTool->read(cmd->offset(), readed_data.data(), cmd->size());
        auto read_ack_func = [data = std::move(readed_data), readed](flatbuffers::FlatBufferBuilder &builder) {
            return Interface::CreateReadAck(builder, builder.CreateVector(data.data(), readed));
        };
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_READ_ACK,
                              Interface::Command::Command_ReadAck, read_ack_func);
        break;
    }
    case Interface::CommandID_REGION_READ: {
        const auto *cmd = msg->body_as_RegionReadCommand();
        if (!cmd || cmd->size() > MAX_REGION_READ_SIZE)
        {
            LOG(m_log, "REGION_READ command: invalid payload or size");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }
        if (!m_memTool)
        {
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }

        std::pmr::vector<uint8_t> readed_data(cmd->size(), &m_pool);
        const auto readed = m_memTool->read(cmd->offset(), readed_data.data(), cmd->size());
        const uintptr_t metadataAddress = cmd->metadata_address() != 0 ? cmd->metadata_address() : cmd->offset();
        auto metadata = query_region_metadata(metadataAddress);
        auto read_ack_func = [data = std::move(readed_data), readed,
                              metadata = std::move(metadata)](flatbuffers::FlatBufferBuilder &builder) {
            flatbuffers::Offset<Interface::MemoryRegionInfo> region;
            if (metadata.available)
            {
                const auto moduleName = builder.CreateString(metadata.moduleName);
                const auto modulePath = builder.CreateString(metadata.modulePath);
                const auto mappedPath = builder.CreateString(metadata.mappedPath);
                region = Interface::CreateMemoryRegionInfo(
                    builder, metadata.address, metadata.baseAddress, metadata.allocationBase,
                    metadata.allocationProtect, metadata.regionSize, metadata.state, metadata.protect, metadata.type,
                    metadata.moduleBase, metadata.moduleSize, moduleName, modulePath, mappedPath, metadata.isHeap,
                    metadata.heapHandle, metadata.heapBlock, metadata.heapBlockSize, metadata.heapFlags,
                    metadata.workingSetQueried, metadata.workingSetValid, metadata.workingSetProtect, metadata.numaNode,
                    metadata.shareCount, metadata.shared, metadata.locked, metadata.largePage, metadata.bad);
            }
            return Interface::CreateRegionReadAck(builder, builder.CreateVector(data.data(), readed), region);
        };
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_REGION_READ_ACK,
                              Interface::Command::Command_RegionReadAck, read_ack_func);
        break;
    }
    case Interface::CommandID_DUMP: {
        LOG(m_log, "Received CommandID_DUMP command");
        if (!m_memTool)
        {
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }
        std::string dump_location = g_params.logDumpLocation + "dump_" + Logger::GetTimestamp() + "\\";
        CreateDirectory(dump_location.c_str(), nullptr);
        MemRead(dump_location, *m_memTool);
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);
        break;
    }
    case Interface::CommandID_FIND: {
        LOG(m_log, "Received CommandID_FIND command");

        const auto *cmd = msg->body_as_FindCommand();
        const auto *data_vector = cmd ? cmd->data() : nullptr;
        if (!cmd || !data_vector || data_vector->size() == 0)
        {
            LOG(m_log, "FIND command: invalid payload");
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_NACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
            break;
        }
        auto value_type = cmd->value_type();
        std::pmr::vector<uint8_t> pattern(data_vector->data(), data_vector->data() + data_vector->size(), &m_pool);
        LOG(m_log, std::format("Pattern size: {}", pattern.size()));

        std::pmr::vector<Region> exludedRegions(&m_pool);
        exludedRegions.emplace_back(reinterpret_cast<uint8_t *>(m_pmrPoolMem),
                                    reinterpret_cast<uint8_t *>(m_pmrPoolMem) + allocate_size);
        uint8_t *m_reciver_address = reinterpret_cast<uint8_t *>(m_reciver.get_shared_buffer_pointer());
        uint8_t *m_sender_address = reinterpret_cast<uint8_t *>(m_sender.get_shared_buffer_pointer());
        exludedRegions.emplace_back(m_reciver_address, m_reciver_address + m_reciver.get_shared_buffer_size());
        exludedRegions.emplace_back(m_sender_address, m_sender_address + m_sender.get_shared_buffer_size());

        std::pmr::vector<FoundOccurrences> results;
        if (m_memTool)
        {
            results = m_memTool->find(pattern, m_pool, std::move(exludedRegions));
        }
        else
        {
            LOG(m_log, "m_memTool is not created");
            return;
        }

        LOG(m_log, std::format("Found {} results", results.size()));

        auto createFindAck = [this, m_reciver_address,
                              m_sender_address](flatbuffers::FlatBufferBuilder &builder, const auto &value_data,
                                                Interface::ValueType value_type,
                                                const auto &occs_vec) -> flatbuffers::Offset<Interface::FindAck> {
            // occurrences
            std::pmr::vector<flatbuffers::Offset<Interface::FoundOccurrences>> occs_fb(&m_pool);
            occs_fb.reserve(occs_vec.size());
            for (const auto &o : occs_vec)
            {
                occs_fb.push_back(Interface::CreateFoundOccurrences(builder, o.baseAddress, o.offset, o.region_size,
                                                                    o.data_size, value_type));
            }

            auto occs_vector = builder.CreateVector(occs_fb);
            auto value_vector = builder.CreateVector(value_data);

            return CreateFindAck(builder, value_vector, value_type, occs_vector);
        };
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_FIND_ACK,
                              Interface::Command::Command_FindAck, createFindAck, pattern, value_type, results);

        break;
    }
    case Interface::CommandID_PRESS_KEY: {
        const auto *cmd = msg->body_as_PressKeyCommand();
        if (!cmd)
        {
            LOG(m_log, "PRESS_KEY command: invalid payload");
            break;
        }
        const HWND hwnd = reinterpret_cast<HWND>(cmd->hwnd());
        const char key = cmd->key();
        SendKeyToWindow(hwnd, key);
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);
        break;
    }
    case Interface::CommandID_STOP: {
        m_state.store(State::Stopping, std::memory_order_release);
        const bool acknowledged =
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
        m_stopAcknowledged.store(acknowledged, std::memory_order_release);
        // Keep consuming until the injector has removed the Windows hook and
        // sends the explicit unload-safe FINALIZE command.
        break;
    }
    case Interface::CommandID_FINALIZE: {
        m_state.store(State::Stopping, std::memory_order_release);
        const bool acknowledged =
            m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK,
                                  Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
        m_stopAcknowledged.store(acknowledged, std::memory_order_release);
        // FINALIZE is only sent after UnhookWindowsHookEx and the target-thread
        // barrier have both completed.
        m_finalizeReceived.store(true, std::memory_order_release);
        m_running.store(false, std::memory_order_release);
        break;
    }
    default:
        break;
    }
}

void SendKeyToWindow(HWND hWnd, char key)
{
    if (!hWnd || !IsWindow(hWnd))
    {
        LOG(MyHook::getInstance().m_log, "Invalid window handle");
        return;
    }

    PostMessage(hWnd, WM_KEYDOWN, key, 0x00000001);
    Sleep(10);
    PostMessage(hWnd, WM_KEYUP, key, 0xC0000001);
    LOG(MyHook::getInstance().m_log, std::format("Sent key '{}' to window", key));
}

void print(std::span<const uint8_t> bytes, std::stringstream &out)
{
    for (auto b : bytes)
    {
        // g_log << std::format("{:02x} ", b);
    }
}

DWORD WINAPI MyHook::MsgConsumerThread()
{
    using namespace Interface;
    std::pmr::vector<uint8_t> buff(&m_pool);
    buff.resize(4);

    if (!m_sender.send_command(0, Interface::CommandID_READY, Interface::Command::Command_NONE,
                               Interface::CreateEmptyCommand))
    {
        return ERROR_WRITE_FAULT;
    }

    while (m_running.load(std::memory_order_acquire))
    {
        uint32_t len = 0;
        std::stringstream ss;
        if (!m_reciver.consume_block(std::span<uint8_t>(reinterpret_cast<uint8_t *>(&len), sizeof(len))))
            break;
        if (len == 0 || len > BUFFER_CAPACITY - sizeof(len))
        {
            LOG(m_log, std::format("Invalid IPC message length: {}", len));
            break;
        }
        buff.resize(len);
        if (!m_reciver.consume_block(std::span<uint8_t>(buff.data(), len)))
            break;
        flatbuffers::Verifier verifier(buff.data(), buff.size());
        if (!VerifyCommandEnvelopeBuffer(verifier))
        {
            LOG(m_log, "Invalid FlatBuffer message");
            continue;
        }
        const auto *message = GetCommandEnvelope(buff.data());
        // STOP is a quiesce request.  Drain queued work, accepting only the
        // post-unhook FINALIZE command.
        if (m_state.load(std::memory_order_acquire) == State::Stopping &&
            message->id() != Interface::CommandID_FINALIZE)
            continue;
        HandleMessage(message);
    }

    return 0;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam) noexcept
{
    if (code >= 0)
    {
        try
        {
            MyHook::getInstance().start();
        }
        catch (const std::exception &error)
        {
            report_boundary_error(nullptr, "HookProc", error.what());
        }
        catch (...)
        {
            report_boundary_error(nullptr, "HookProc");
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

// DllMain
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
