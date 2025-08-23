#ifndef UTILITY_H
#define UTILITY_H

#include "common/common.h"
#include <bit>
#include <format>
#include <fstream>
#include <mutex>
#include <span>
#include <windows.h>

#include "shared_buffer.hpp"

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
    MessageIPCSender(const std::string &shm_name, bool create);
    ~MessageIPCSender();
    void send(std::span<const uint8_t> bytes);

    template <typename TCreateFn, typename... Args>
    bool send_command(Interface::CommandID id, Interface::Command type, TCreateFn create_fn, Args &&...args)
    {
        std::scoped_lock lck(m_mutex);

        flatbuffers::FlatBufferBuilder builder;

        auto body = create_fn(builder, std::forward<Args>(args)...);

        auto envelope = Interface::CreateCommandEnvelope(builder, id, type, body.Union());
        builder.Finish(envelope);

        auto buf_ptr = builder.GetBufferPointer();

        const uint32_t len = builder.GetSize();

        m_buffer.resize(len + sizeof(len));
        std::memcpy(m_buffer.data(), &len, sizeof(len));
        std::memcpy(m_buffer.data() + sizeof(uint32_t), buf_ptr, len);

        return m_sharedBufferTx.produce_block(m_buffer);
    }

    inline void * get_sharred_buffer_pointer()
    {
        return m_sharedBufferTx.get_sharred_buffer_pointer();
    }

    void close();
    void reset();

  private:
    std::mutex m_mutex;
    std::vector<uint8_t> m_buffer;
    SharedBuffer<BUFFER_CAPACITY> m_sharedBufferTx;
};

std::string valueToString(const flatbuffers::Vector<uint8_t> *value, Interface::ValueType type);

#endif // UTILITY_H
