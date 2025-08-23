#include "utility.h"

MessageIPCSender::MessageIPCSender(const std::string &shm_name, bool create) : m_sharedBufferTx(shm_name, create)
{
}

MessageIPCSender::~MessageIPCSender()
{
    m_sharedBufferTx.close();
}

void MessageIPCSender::send(std::span<const uint8_t> bytes)
{
    std::scoped_lock lck(m_mutex);
    uint32_t len = bytes.size();
    m_buffer.resize(len + sizeof(len));
    std::memcpy(m_buffer.data(), &len, sizeof(len));
    std::copy(bytes.begin(), bytes.end(), m_buffer.begin() + sizeof(len));

    m_sharedBufferTx.produce_block(m_buffer);
}

void MessageIPCSender::close()
{
    m_sharedBufferTx.close();
}

void MessageIPCSender::reset()
{
    m_sharedBufferTx.reset();
}

std::string valueToString(const flatbuffers::Vector<uint8_t>* value,
                          Interface::ValueType type)
{
    using namespace Interface;
    if (!value || value->size() == 0) return {};

    std::span<const uint8_t> data(value->data(), value->size());

    auto to_hex = [&]() -> std::string {
        std::string out;
        out.reserve(data.size() * 2);
        for (uint8_t b : data)
            out += std::format("{:02X}", b);
        return out;
    };

    switch (type) {
        case ValueType::ValueType_Int32:
            if (data.size() >= sizeof(int32_t))
                return std::format("{}", std::bit_cast<int32_t>(*reinterpret_cast<const int32_t*>(data.data())));
            break;

        case ValueType::ValueType_Float:
            if (data.size() >= sizeof(float))
                return std::format("{}", std::bit_cast<float>(*reinterpret_cast<const float*>(data.data())));
            break;

        case ValueType::ValueType_Double:
            if (data.size() >= sizeof(double))
                return std::format("{}", std::bit_cast<double>(*reinterpret_cast<const double*>(data.data())));
            break;

        case ValueType::ValueType_Int64:
            if (data.size() >= sizeof(int64_t))
                return std::format("{}", std::bit_cast<int64_t>(*reinterpret_cast<const int64_t*>(data.data())));
            break;

        case ValueType::ValueType_String:
            return std::string(reinterpret_cast<const char*>(data.data()), data.size());

        case ValueType::ValueType_String16: {
            if (data.size() % 2 == 0) {
                std::u16string str16(reinterpret_cast<const char16_t*>(data.data()), data.size() / 2);
                std::string utf8;
                utf8.reserve(str16.size());
                for (char16_t c : str16) utf8 += static_cast<char>(c); // упрощённо
                return utf8;
            }
            break;
        }

        case ValueType::ValueType_ByteArray:
            return to_hex();

        default:
            break;
    }

    return {};
}