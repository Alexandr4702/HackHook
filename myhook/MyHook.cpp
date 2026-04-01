#include <atomic>
#include <cstddef>
#include <fstream>
#include <thread>
#include <string>
#include <sstream>
#include <iomanip>
#include <array>
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

void SendKeyToWindow(HWND hWnd, char key);

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
    m_log << "[MyHook] Destroyed\n";

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
    if (m_threadStarted.exchange(true))
        return;

    m_reciver.init(BUFFER_NAME_TX, BUFFER_CAPACITY, false);
    m_sender.init(BUFFER_NAME_RX, false);
    m_running.store(true, std::memory_order_release);
    CreateThread(nullptr, 0, &MyHook::ThreadWrapperCreator, this, 0, nullptr);
}

void MyHook::stop()
{
    m_running.store(false, std::memory_order_release);
    m_log << "[MyHook] Stopped\n";
}

DWORD WINAPI MyHook::ThreadWrapperCreator(LPVOID param)
{
    return static_cast<MyHook *>(param)->ThreadsCreator();
}

DWORD WINAPI MyHook::ThreadWrapperMsg(LPVOID param)
{
    return static_cast<MyHook *>(param)->MsgConsumerThread();
}

void MyHook::HandleMessage(const Interface::CommandEnvelope *msg)
{
    if (msg == nullptr)
        return;

    switch (msg->id())
    {
    case Interface::CommandID_WRITE:
        m_log << std::format("[MyHook] Received write command with offset: {}\n", msg->body_as_WriteCommand()->offset());

        break;
    case Interface::CommandID_READ:
        m_log << std::format("[MyHook] Received read command with offset: {}\n", msg->body_as_ReadCommand()->offset());
        break;
    case Interface::CommandID_DUMP: {
        m_log << "[MyHook] Received CommandID_DUMP command \n";
        std::string dump_location = g_params.logDumpLocation + "dump_" + Logger::GetTimestamp() + "\\";
        CreateDirectory(dump_location.c_str(), nullptr);
        MemRead(dump_location, m_log);
        m_sender.send_command(Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);
        break;
    }
    case Interface::CommandID_FIND: {
        m_log << "[MyHook] Received CommandID_FIND command \n";

        auto data_vector = msg->body_as_FindCommand()->data();
        auto value_type = msg->body_as_FindCommand()->value_type();
        std::pmr::vector<uint8_t> pattern(data_vector->data(), data_vector->data() + data_vector->size(), &m_pool);
        m_log << std::format("[MyHook] Pattern size: {} \n", pattern.size());

        std::pmr::vector<Region> exludedRegions(&m_pool);
        exludedRegions.emplace_back(reinterpret_cast<uint8_t*>(m_pmrPoolMem), reinterpret_cast<uint8_t*>(m_pmrPoolMem) + allocate_size);
        uint8_t* m_reciver_address = reinterpret_cast<uint8_t*>(m_reciver.get_shared_buffer_pointer());
        uint8_t* m_sender_address = reinterpret_cast<uint8_t*>(m_sender.get_shared_buffer_pointer());
        exludedRegions.emplace_back(m_reciver_address, m_reciver_address + m_reciver.get_shared_buffer_size());
        exludedRegions.emplace_back(m_sender_address, m_sender_address + m_sender.get_shared_buffer_size());
        auto  results = find(pattern, m_pool, std::move(exludedRegions));

        m_log << std::format("[MyHook] Found {} results \n", results.size());

        auto createFindAck = [this, m_reciver_address, m_sender_address](flatbuffers::FlatBufferBuilder &builder, const auto& value_data,
                                Interface::ValueType value_type,
                                const auto& occs_vec) -> flatbuffers::Offset<Interface::FindAck> {
            // occurrences
            std::pmr::vector<flatbuffers::Offset<Interface::FoundOccurrences>> occs_fb(&m_pool);
            occs_fb.reserve(occs_vec.size());
            for (const auto &o : occs_vec)
            {
                occs_fb.push_back(
                    Interface::CreateFoundOccurrences(builder, o.baseAddress, o.offset, o.region_size, o.data_size, o.type));
            }

            auto occs_vector = builder.CreateVector(occs_fb);
            auto value_vector = builder.CreateVector(value_data);

            return CreateFindAck(builder, value_vector, value_type, occs_vector);
        };

        m_sender.send_command(Interface::CommandID::CommandID_FIND_ACK, Interface::Command::Command_FindAck, createFindAck, pattern, value_type, results);

        break;
    }
    case Interface::CommandID_PRESS_KEY: {
        const HWND hwnd = reinterpret_cast<HWND>(msg->body_as_PressKeyCommand()->hwnd());
        const char key = msg->body_as_PressKeyCommand()->key();
        SendKeyToWindow(hwnd, key);
        m_sender.send_command(Interface::CommandID::CommandID_ACK, Interface::Command::Command_NONE,
                              Interface::CreateEmptyCommand);
        break;
    }
    default:
        break;
    }
}

DWORD MyHook::ThreadsCreator()
{
    std::string folderName = g_params.logDumpLocation + "dump_" + Logger::GetTimestamp() + "\\";
    g_params.logDumpLocation = folderName + "\\";
    CreateDirectory(folderName.c_str(), nullptr);
    m_log.init(g_params.logDumpLocation + "log.txt");

    m_threadsHandler.emplace_back(
        std::jthread(&MyHook::ThreadWrapperMsg, this));

    return 0;
}


void SendKeyToWindow(HWND hWnd, char key)
{
    if (!hWnd || !IsWindow(hWnd))
    {
        MyHook::getInstance().m_log << "[ERROR] Invalid window handle\n";
        return;
    }

    PostMessage(hWnd, WM_KEYDOWN, key, 0x00000001);
    Sleep(10);
    PostMessage(hWnd, WM_KEYUP, key, 0xC0000001);
    MyHook::getInstance().m_log << std::format("[KEY] Sent key '{}' to window\n", key);
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
        buff.resize(len);
        if (!m_reciver.consume_block(std::span<uint8_t>(buff.data(), len)))
            break;
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
    else if (reason == DLL_PROCESS_DETACH)
    {
        MyHook::getInstance().stop();
    }
    return TRUE;
}