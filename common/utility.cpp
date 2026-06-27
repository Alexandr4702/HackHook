#include "utility.h"

namespace
{
void write_emergency_log(const char *path, const char *boundary, const char *details) noexcept
{
    if (!path || !boundary)
        return;

    char message[2048]{};
    size_t used = 0;
    auto append = [&message, &used](const char *text) noexcept {
        if (!text)
            return;
        while (*text && used + 1 < sizeof(message))
            message[used++] = *text++;
    };

    append("[myhook] exception in ");
    append(boundary);
    if (details)
    {
        append(": ");
        append(details);
    }
    append("\r\n");

    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        OutputDebugStringA(message);
        return;
    }

    DWORD written = 0;
    WriteFile(file, message, static_cast<DWORD>(used), &written, nullptr);
    CloseHandle(file);
}

const char *default_emergency_path(char (&path)[MAX_PATH]) noexcept
{
    static constexpr char filename[] = "myhook_errors.log";
    const DWORD length = GetTempPathA(MAX_PATH, path);
    if (length == 0 || length >= MAX_PATH || length + sizeof(filename) > MAX_PATH)
        return filename;

    std::memcpy(path + length, filename, sizeof(filename));
    return path;
}

void append_utf8(std::string &out, uint32_t code_point)
{
    if (code_point <= 0x7F)
    {
        out.push_back(static_cast<char>(code_point));
    }
    else if (code_point <= 0x7FF)
    {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else if (code_point <= 0xFFFF)
    {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
    else
    {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::string utf16_to_utf8(std::span<const uint8_t> data)
{
    if (data.size() % sizeof(char16_t) != 0)
        return {};

    std::string out;
    out.reserve(data.size());

    for (size_t i = 0; i < data.size(); i += sizeof(char16_t))
    {
        char16_t first{};
        std::memcpy(&first, data.data() + i, sizeof(first));
        uint32_t code_point = first;

        if (first >= 0xD800 && first <= 0xDBFF && i + 2 * sizeof(char16_t) <= data.size())
        {
            char16_t second{};
            std::memcpy(&second, data.data() + i + sizeof(char16_t), sizeof(second));
            if (second >= 0xDC00 && second <= 0xDFFF)
            {
                code_point = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                i += sizeof(char16_t);
            }
            else
            {
                code_point = 0xFFFD;
            }
        }
        else if (first >= 0xD800 && first <= 0xDFFF)
        {
            code_point = 0xFFFD;
        }

        append_utf8(out, code_point);
    }

    return out;
}
} // namespace

void Logger::emergency(const char *boundary, const char *details) const noexcept
{
    if (!emergency_path_ready.load(std::memory_order_acquire))
    {
        emergency_to_default(boundary, details);
        return;
    }

    write_emergency_log(emergency_path.data(), boundary, details);
}

void Logger::emergency_to_default(const char *boundary, const char *details) noexcept
{
    char path[MAX_PATH]{};
    write_emergency_log(default_emergency_path(path), boundary, details);
}

void MessageIPCSender::init(const std::string &shm_name, bool create)
{
    m_sharedBufferTx.init(shm_name, BUFFER_CAPACITY, create);
}

MessageIPCSender::~MessageIPCSender() noexcept
{
    release();
}

bool MessageIPCSender::send(std::span<const uint8_t> bytes) noexcept
{
    try
    {
        if (!m_sharedBufferTx.is_initialized()) return false;
        std::scoped_lock lck(m_mutex);
        uint32_t len = bytes.size();
        m_buffer.resize(len + sizeof(len));
        std::memcpy(m_buffer.data(), &len, sizeof(len));
        std::copy(bytes.begin(), bytes.end(), m_buffer.begin() + sizeof(len));

        return m_sharedBufferTx.produce_block(m_buffer);
    }
    catch (...)
    {
        return false;
    }
}

void MessageIPCSender::close() noexcept
{
    m_sharedBufferTx.close();
}

void MessageIPCSender::reset() noexcept
{
    m_sharedBufferTx.reset();
}

void MessageIPCSender::release(bool close_buffer) noexcept
{
    try
    {
        std::scoped_lock lock(m_mutex);
        m_sharedBufferTx.release(close_buffer);
        m_builder.Reset();
        std::vector<uint8_t>{}.swap(m_buffer);
    }
    catch (...)
    {
    }
}

std::string valueToString(std::span<const uint8_t> data,
                          Interface::ValueType type)
{
    using namespace Interface;
    if (data.empty()) return {};

    auto to_hex = [&]() -> std::string {
        std::string out;
        out.reserve(data.size() * 2);
        for (uint8_t b : data)
            out += std::format("{:02X}", b);
        return out;
    };

    switch (type)
    {
        case ValueType_Int32:
        {
            if (data.size() < sizeof(int32_t)) break;
            int32_t v;
            std::memcpy(&v, data.data(), sizeof(v));
            return std::format("{}", v);
        }

        case ValueType_Int64:
        {
            if (data.size() < sizeof(int64_t)) break;
            int64_t v;
            std::memcpy(&v, data.data(), sizeof(v));
            return std::format("{}", v);
        }

        case ValueType_Float:
        {
            if (data.size() < sizeof(float)) break;
            float v;
            std::memcpy(&v, data.data(), sizeof(v));
            return std::format("{}", v);
        }

        case ValueType_Double:
        {
            if (data.size() < sizeof(double)) break;
            double v;
            std::memcpy(&v, data.data(), sizeof(v));
            return std::format("{}", v);
        }

        case ValueType_String:
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());

        case ValueType_String16:
            return utf16_to_utf8(data);

        case ValueType_ByteArray:
            return to_hex();

        default:
            break;
    }

    return {};
}

std::string valueToString(const flatbuffers::Vector<uint8_t>* value,
                          Interface::ValueType type)
{
    using namespace Interface;
    if (!value || value->size() == 0) return {};

    std::span<const uint8_t> data(value->data(), value->size());

    return valueToString(data, type);
}
