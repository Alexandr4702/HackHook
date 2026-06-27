#include <atomic>
#include <cstddef>
#include <fstream>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <array>
#include <utility>
#include <vector>
#include <unordered_map>
#include <format>

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>
#include <psapi.h>

#include "MyHook.h"
#include "MemDumper.h"
#include "MemoryScanner.h"
#include "interface_generated.h"

void SendKeyToWindow(HWND hWnd, char key);
extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam);

MyHook::MyHook(): allocate_size(128 * 1024 * 1024), m_pmrPoolMem(VirtualAlloc(nullptr, allocate_size,
                                MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE)), m_monotonicPool(m_pmrPoolMem, allocate_size), m_pool(&m_monotonicPool)
{
    // std::string folderName = g_params.logDumpLocation + "dump_" + GetTimestamp() + "\\";
    // g_params.logDumpLocation = folderName + "\\";
    // CreateDirectory(folderName.c_str(), nullptr);
    // g_log.init();

    // m_pmrPool = std::pmr::monotonic_buffer_resource(m_pmrPoolMem, allocate_size);
    // std::pmr::synchronized_pool_resource pool(&m_pmrPool);
}

MyHook::~MyHook()
{
    LOG(m_log, "MyHook Destroyed");

    m_pool.release();
    m_monotonicPool.release();

    if (m_pmrPoolMem)
    {
        VirtualFree(m_pmrPoolMem, 0, MEM_RELEASE);
        m_pmrPoolMem = nullptr;
    }
}

void MyHook::start()
{
    if (m_stopping.load(std::memory_order_acquire))
        return;

    if (m_threadStarted.exchange(true))
        return;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCSTR>(&HookProc), &m_moduleRef))
    {
        m_threadStarted.store(false, std::memory_order_release);
        return;
    }

    m_memTool.emplace();
    m_reciver.init(BUFFER_NAME_TX, BUFFER_CAPACITY, false);
    m_sender.init(BUFFER_NAME_RX, false);
    m_running.store(true, std::memory_order_release);

    std::string folderName = g_params.logDumpLocation + "dump_" + Logger::GetTimestamp() + "\\";
    g_params.logDumpLocation = folderName + "\\";
    CreateDirectory(folderName.c_str(), nullptr);
    m_log.init(g_params.logDumpLocation + "log.txt");

    HANDLE worker = CreateThread(nullptr, 0, &MyHook::ThreadWrapperMsg, this, 0, nullptr);
    if (!worker)
    {
        m_running.store(false, std::memory_order_release);
        m_threadStarted.store(false, std::memory_order_release);
        m_memTool.reset();
        m_reciver.close();
        m_sender.close();

        HMODULE module = std::exchange(m_moduleRef, nullptr);
        if (module)
            FreeLibrary(module);
        return;
    }
    CloseHandle(worker);
}

void MyHook::stop()
{
    if (!m_threadStarted.exchange(false))
        return;

    m_stopping.store(true, std::memory_order_release);
    m_running.store(false, std::memory_order_release);
    m_reciver.close();
    m_sender.close();
    LOG(m_log, "MyHook Stopped");
}

DWORD WINAPI MyHook::ThreadWrapperMsg(LPVOID param)
{
    auto *self = static_cast<MyHook *>(param);
    const DWORD result = self->MsgConsumerThread();

    self->m_running.store(false, std::memory_order_release);
    self->m_threadStarted.store(false, std::memory_order_release);
    self->m_reciver.close();
    if (!self->m_stopping.load(std::memory_order_acquire))
        self->m_sender.close();
    self->m_memTool.reset();

    HMODULE module = std::exchange(self->m_moduleRef, nullptr);
    if (module)
        FreeLibraryAndExitThread(module, result);

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
            break;
        }

        bool ok = m_memTool->write(offset, data->data(), size);

        if (!ok)
        {
            LOG(m_log, "WRITE failed");
        }

        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);

        break;
    }
    case Interface::CommandID_READ: {
        const auto *cmd = msg->body_as_ReadCommand();
        if (!cmd || cmd->size() > BUFFER_CAPACITY - 1024)
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
        auto read_ack_func = [data = std::move(readed_data), readed](flatbuffers::FlatBufferBuilder &builder) -> flatbuffers::Offset<Interface::ReadAck> {
            return Interface::CreateReadAck(builder, builder.CreateVector(data.data(), readed));
        };
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_READ_ACK,
                              Interface::Command::Command_ReadAck, read_ack_func);
        break;
    }
    case Interface::CommandID_DUMP: {
        LOG(m_log, "Received CommandID_DUMP command");
        std::string dump_location = g_params.logDumpLocation + "dump_" + Logger::GetTimestamp() + "\\";
        CreateDirectory(dump_location.c_str(), nullptr);
        MemRead(dump_location);
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
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
        m_stopping.store(true, std::memory_order_release);
        m_running.store(false, std::memory_order_release);
        m_sender.send_command(msg->request_id(), Interface::CommandID::CommandID_ACK,
                              Interface::Command::Command_NONE, Interface::CreateEmptyCommand);
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
        HandleMessage(GetCommandEnvelope(buff.data()));
    }

    return 0;
}

extern "C" __declspec(dllexport) LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0)
    {
        MyHook::getInstance().start();
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
