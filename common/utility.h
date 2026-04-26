#ifndef UTILITY_H
#define UTILITY_H

#include "common/common.h"
#include <algorithm>
#include <bit>
#include <format>
#include <fstream>
#include <mutex>
#include <span>
#include <windows.h>

#include "SharedBuffer.h"

class Logger
{
    std::ofstream file;
    mutable std::mutex log_mutex;

  public:
    Logger()
    {
    }

    ~Logger()
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (file.is_open())
        {
            file.close();
        }
    }

    void init(const std::string &filename)
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        file.open(filename, std::ios::out | std::ios::app);
    }

    template <typename T> Logger &operator<<(T &&value)
    {
        log_impl(std::forward<T>(value));
        return *this;
    }

    Logger &operator<<(std::ostream &(*manip)(std::ostream &))
    {
        log_impl(manip);
        return *this;
    }

    static std::string GetTimestamp()
    {
        SYSTEMTIME st;
        GetLocalTime(&st);
        char buffer[64];
        sprintf_s(buffer, "%04d-%02d-%02d_%02d-%02d-%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
                  st.wSecond);
        return buffer;
    }

  private:
    template <typename T> void log_impl(T &&value)
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        if (file.is_open())
        {
            file << "[" << GetTimestamp() << "] ";
            file << std::forward<T>(value);
            file.flush();
        }
    }

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
};

class MessageIPCSender
{
  public:
    MessageIPCSender() = default;
    ~MessageIPCSender();
    void init(const std::string &shm_name, bool create);
    bool send(std::span<const uint8_t> bytes);

    template <typename TCreateFn, typename... Args>
    bool send_command(Interface::CommandID id, Interface::Command type, TCreateFn create_fn, Args &&...args)
    {
        if (!m_sharedBufferTx.is_initialized()) return false;

        std::scoped_lock lck(m_mutex);

        auto body = create_fn(m_builder, std::forward<Args>(args)...);

        auto envelope = Interface::CreateCommandEnvelope(m_builder, id, type, body.Union());
        m_builder.Finish(envelope);

        auto buf_ptr = m_builder.GetBufferPointer();

        const uint32_t len = m_builder.GetSize();

        m_buffer.resize(len + sizeof(len));
        std::memcpy(m_buffer.data(), &len, sizeof(len));
        std::memcpy(m_buffer.data() + sizeof(uint32_t), buf_ptr, len);

        bool result = m_sharedBufferTx.produce_block(m_buffer);
        std::fill(m_buffer.begin(), m_buffer.end(), 0);
        std::memset(m_builder.GetBufferPointer(), 0, m_builder.GetSize());
        m_builder.Clear();
        return result;
    }

    inline void * get_shared_buffer_pointer()
    {
        return m_sharedBufferTx.get_shared_buffer_pointer();
    }
    inline const size_t get_shared_buffer_size()
    {
        return m_sharedBufferTx.m_size;
    }

    void close();
    void reset();

  private:
    std::mutex m_mutex;
    flatbuffers::FlatBufferBuilder m_builder;
    std::vector<uint8_t> m_buffer;
    SharedBuffer m_sharedBufferTx;
};

std::string valueToString(const flatbuffers::Vector<uint8_t> *value, Interface::ValueType type);
std::string valueToString(std::span<const uint8_t> data, Interface::ValueType type);
#endif // UTILITY_H
