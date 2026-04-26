#include "utility.h"

void MessageIPCSender::init(const std::string &shm_name, bool create)
{
    m_sharedBufferTx.init(shm_name, BUFFER_CAPACITY, create);
}

MessageIPCSender::~MessageIPCSender()
{
    m_sharedBufferTx.close();
}

bool MessageIPCSender::send(std::span<const uint8_t> bytes)
{
    if (!m_sharedBufferTx.is_initialized()) return false;
    std::scoped_lock lck(m_mutex);
    uint32_t len = bytes.size();
    m_buffer.resize(len + sizeof(len));
    std::memcpy(m_buffer.data(), &len, sizeof(len));
    std::copy(bytes.begin(), bytes.end(), m_buffer.begin() + sizeof(len));

    return m_sharedBufferTx.produce_block(m_buffer);
}

void MessageIPCSender::close()
{
    m_sharedBufferTx.close();
}

void MessageIPCSender::reset()
{
    m_sharedBufferTx.reset();
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
        {
            if (data.size() % 2 != 0) break;

            std::u16string str16(
                reinterpret_cast<const char16_t*>(data.data()),
                data.size() / 2
            );

            std::string utf8;
            utf8.reserve(str16.size());

            for (char16_t c : str16)
                utf8 += static_cast<char>(c); // всё ещё упрощённо

            return utf8;
        }

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