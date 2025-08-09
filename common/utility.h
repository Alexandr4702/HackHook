#ifndef UTILITY_H
#define UTILITY_H

#include <mutex>
#include <string>
#include <fstream>
#include <mutex>
#include "common/common.h"
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
        file.open(filename,
                  std::ios::out | std::ios::app);
    }

    template <typename T>
    Logger &operator<<(T &&value)
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
        sprintf_s(buffer, "%04d-%02d-%02d_%02d-%02d-%02d",
                  st.wYear, st.wMonth, st.wDay,
                  st.wHour, st.wMinute, st.wSecond);
        return buffer;
    }

private:
    template <typename T>
    void log_impl(T &&value)
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
    void close();

private:
    std::mutex m_mutex;
    std::vector<uint8_t> m_buffer;
    SharedBuffer<BUFFER_CAPACITY> SharedBufferTx;
};

#endif // UTILITY_H
