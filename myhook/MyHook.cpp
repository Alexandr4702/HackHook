#include <atomic>
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

MyHook::MyHook() : m_reciver(BUFFER_NAME_TX, false), m_sender(BUFFER_NAME_RX, false)
{
    // std::string folderName = g_params.logDumpLocation + "dump_" + GetTimestamp() + "\\";
    // g_params.logDumpLocation = folderName + "\\";
    // CreateDirectory(folderName.c_str(), nullptr);
    // g_log.init();
}

void MyHook::start()
{
    if (m_threadStarted.exchange(true))
        return;

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

// DWORD WINAPI MyHook::ThreadWrapperKey(LPVOID param)
// {
//     return static_cast<MyHook *>(param)->KeyPressThread();
// }

// DWORD WINAPI MyHook::ThreadWrapperMem(LPVOID param)
// {
//     return static_cast<MyHook *>(param)->MemReadThread();
// }

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
        m_log << "[MyHook] Received write command with offset: " << msg->body_as_WriteCommand()->offset() << "\n";

        break;
    case Interface::CommandID_READ:
        m_log << "[MyHook] Received read command with offset: " << msg->body_as_ReadCommand()->offset() << "\n";
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

        std::vector<uint8_t> pattern = {data_vector->data(), data_vector->data() + data_vector->size()};
        
        m_log << std::format("[MyHook] Pattern size: {} \n", pattern.size());
        std::vector<FoundOccurrences> results = find(pattern);
        uint64_t m_reciver_address = reinterpret_cast<uint64_t>(m_reciver.get_sharred_buffer_pointer());
        uint64_t m_sender_address = reinterpret_cast<uint64_t>(m_sender.get_sharred_buffer_pointer());

        m_log << std::format("[MyHook] Found {} results \n", results.size());

        auto createFindAck = [m_reciver_address, m_sender_address](flatbuffers::FlatBufferBuilder &builder, std::vector<uint8_t> value_data,
                                Interface::ValueType value_type,
                                const std::vector<FoundOccurrences> &occs_vec) -> flatbuffers::Offset<Interface::FindAck> {
            // occurrences
            std::vector<flatbuffers::Offset<Interface::FoundOccurrences>> occs_fb;
            for (const auto &o : occs_vec)
            {
                if (o.baseAddress == m_reciver_address || o.baseAddress == m_sender_address)
                {
                    continue;
                }
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
        // g_log << "[ERROR] Invalid window handle\n";
        return;
    }

    PostMessage(hWnd, WM_KEYDOWN, key, 0x00000001);
    Sleep(10);
    PostMessage(hWnd, WM_KEYUP, key, 0xC0000001);

    // g_log << "[KEY] Sent key '" << key << "' to window\n";
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