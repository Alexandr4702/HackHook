#include "utility.h"

MessageIPCSender::MessageIPCSender(const std::string &shm_name, bool create)
    : SharedBufferTx(shm_name, create)
{
}

MessageIPCSender::~MessageIPCSender()
{
    SharedBufferTx.close();
}

void MessageIPCSender::send(std::span<const uint8_t> bytes)
{
    std::scoped_lock lck(m_mutex);
    uint32_t len = bytes.size();
    m_buffer.resize(len + sizeof(len));
    std::memcpy(m_buffer.data(), &len, sizeof(len));
    std::copy(bytes.begin(), bytes.end(), m_buffer.begin() + sizeof(len));

    SharedBufferTx.produce_block(m_buffer);
}

void MessageIPCSender::close()
{
    SharedBufferTx.close();
}

void MessageIPCSender::reset()
{
    SharedBufferTx.reset();
}