#ifndef UTILITY_H
#define UTILITY_H

#include "common/common.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
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
    std::array<char, MAX_PATH> emergency_path{};
    std::atomic_bool emergency_path_ready = false;

public:
    Logger() = default;

    ~Logger()
    {
        close();
    }

    void close() noexcept
    {
        try
        {
            std::scoped_lock lock(log_mutex);
            if (file.is_open())
                file.close();
        }
        catch (...)
        {
        }
    }

    void init(const std::string& filename)
    {
        std::scoped_lock lock(log_mutex);
        if (file.is_open())
            return;

        const size_t path_size = std::min(filename.size(), emergency_path.size() - 1);
        std::memcpy(emergency_path.data(), filename.data(), path_size);
        emergency_path[path_size] = '\0';
        emergency_path_ready.store(true, std::memory_order_release);
        file.open(filename, std::ios::out | std::ios::app);
    }

    void emergency(const char *boundary, const char *details = nullptr) const noexcept;
    static void emergency_to_default(const char *boundary, const char *details = nullptr) noexcept;

    template <typename T>
    Logger& operator<<(T&& value)
    {
        std::scoped_lock lock(log_mutex);

        if (!file.is_open())
            return *this;

        file << "[" << GetTimestamp() << "] "
             << std::forward<T>(value);

        return *this;
    }

    Logger& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        std::scoped_lock lock(log_mutex);

        if (!file.is_open())
            return *this;

        file << manip;
        return *this;
    }
    static std::string GetTimestamp()
    {
        using namespace std::chrono;

        auto now = system_clock::now();
        auto t = system_clock::to_time_t(now);

        std::tm tm;
        localtime_s(&tm, &t);

        return std::format("{:04}-{:02}-{:02}_{:02}-{:02}-{:02}",
                           tm.tm_year + 1900,
                           tm.tm_mon + 1,
                           tm.tm_mday,
                           tm.tm_hour,
                           tm.tm_min,
                           tm.tm_sec);
    }
private:

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
};

#define LOG(logger, msg) \
    (logger) << std::format("[{}: {} ] {} \n", __FILE__, __LINE__, msg)

class MessageIPCSender
{
  public:
    MessageIPCSender() = default;
    ~MessageIPCSender() noexcept;
    void init(const std::string &shm_name, bool create);
    bool send(std::span<const uint8_t> bytes) noexcept;

    template <typename TCreateFn, typename... Args>
    bool send_command(uint64_t request_id, Interface::CommandID id, Interface::Command type,
                      TCreateFn create_fn, Args &&...args) noexcept
    {
        try
        {
            if (!m_sharedBufferTx.is_initialized()) return false;

            std::scoped_lock lck(m_mutex);

            auto body = create_fn(m_builder, std::forward<Args>(args)...);

            auto envelope = Interface::CreateCommandEnvelope(m_builder, id, request_id, type, body.Union());
            m_builder.Finish(envelope);

            auto buf_ptr = m_builder.GetBufferPointer();

            const uint32_t len = m_builder.GetSize();

            m_buffer.resize(len + sizeof(len));
            std::memcpy(m_buffer.data(), &len, sizeof(len));
            std::memcpy(m_buffer.data() + sizeof(uint32_t), buf_ptr, len);

            const bool result = m_sharedBufferTx.produce_block(m_buffer);
            std::fill(m_buffer.begin(), m_buffer.end(), 0);
            std::memset(m_builder.GetBufferPointer(), 0, m_builder.GetSize());
            m_builder.Clear();
            return result;
        }
        catch (...)
        {
            try
            {
                std::scoped_lock cleanup_lock(m_mutex);
                m_builder.Reset();
                m_buffer.clear();
            }
            catch (...)
            {
            }
            return false;
        }
    }

    inline void * get_shared_buffer_pointer()
    {
        return m_sharedBufferTx.get_shared_buffer_pointer();
    }
    inline const size_t get_shared_buffer_size()
    {
        return m_sharedBufferTx.m_size;
    }

    void close() noexcept;
    void reset() noexcept;
    void release(bool close_buffer = true) noexcept;

  private:
    std::mutex m_mutex;
    flatbuffers::FlatBufferBuilder m_builder;
    std::vector<uint8_t> m_buffer;
    SharedBuffer m_sharedBufferTx;
};

std::string valueToString(const flatbuffers::Vector<uint8_t> *value, Interface::ValueType type);
std::string valueToString(std::span<const uint8_t> data, Interface::ValueType type);
#endif // UTILITY_H
