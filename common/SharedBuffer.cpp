#include "common/SharedBuffer.h"
#include <algorithm>

void SharedBuffer::init(const std::string &shm_name, std::size_t capacity, bool create)
{
    // Cleanup old buffer if re-init
    if (m_buf)
    {
        if (m_creator)
            bip::shared_memory_object::remove(m_shm_name.c_str());

        m_buf = nullptr;
        start_segment_ptr = nullptr;
    }

    m_creator = create;
    if (m_creator)
    {
        bip::shared_memory_object::remove(shm_name.c_str());

        std::size_t shm_size = sizeof(Buffer) + capacity + 64 * 1024;

        bip::permissions perms;
        perms.set_unrestricted();

        m_shm = bip::managed_shared_memory(
            bip::create_only, shm_name.c_str(), shm_size, 0, perms);

        m_buf = m_shm.construct<Buffer>("buffer")();
        m_buf->capacity = capacity;

        uint8_t* data_ptr = m_shm.construct<uint8_t>("data")[capacity]();
        m_buf->data = data_ptr;
    }
    else
    {
        m_shm = bip::managed_shared_memory(bip::open_only, shm_name.c_str());
        m_buf = m_shm.find<Buffer>("buffer").first;
    }

    start_segment_ptr = m_shm.get_address();
}

SharedBuffer::~SharedBuffer()
{
    if (m_creator)
        bip::shared_memory_object::remove(m_shm_name.c_str());
}

bool SharedBuffer::produce_block(std::span<const uint8_t> src)
{
    if (!m_buf) return false;

    bip::scoped_lock lock(m_buf->mutex);
    m_buf->not_full.wait(lock, [&] { return m_buf->available_space() >= src.size() || m_buf->closed; });

    if (m_buf->closed || m_buf->available_space() < src.size()) return false;

    std::size_t first_chunk = std::min(src.size(), m_buf->capacity - m_buf->head);
    std::copy(src.begin(), src.begin() + first_chunk, m_buf->data.get() + m_buf->head);

    std::size_t remaining = src.size() - first_chunk;
    if (remaining > 0)
        std::copy(src.begin() + first_chunk, src.end(), m_buf->data.get());

    m_buf->head = (m_buf->head + src.size()) % m_buf->capacity;
    m_buf->count += src.size();

    m_buf->not_empty.notify_all();
    return true;
}

bool SharedBuffer::consume_block(std::span<uint8_t> dest)
{
    if (!m_buf) return false;

    bip::scoped_lock lock(m_buf->mutex);
    m_buf->not_empty.wait(lock, [&] { return m_buf->count >= dest.size() || m_buf->closed; });

    if (m_buf->closed || m_buf->count < dest.size()) return false;

    std::size_t first_chunk = std::min(dest.size(), m_buf->capacity - m_buf->tail);
    std::copy(m_buf->data.get() + m_buf->tail, m_buf->data.get() + m_buf->tail + first_chunk, dest.begin());

    std::size_t remaining = dest.size() - first_chunk;
    if (remaining > 0)
        std::copy(m_buf->data.get(), m_buf->data.get() + remaining, dest.begin() + first_chunk);

    m_buf->tail = (m_buf->tail + dest.size()) % m_buf->capacity;
    m_buf->count -= dest.size();

    m_buf->not_full.notify_all();
    return true;
}

void SharedBuffer::close()
{
    if (!m_buf) return;
    bip::scoped_lock lock(m_buf->mutex);
    m_buf->closed = true;
    m_buf->not_empty.notify_all();
    m_buf->not_full.notify_all();
}

void SharedBuffer::reset()
{
    if (!m_buf) return;
    bip::scoped_lock lock(m_buf->mutex);
    m_buf->closed = false;
    m_buf->head = 0;
    m_buf->tail = 0;
    m_buf->count = 0;
}
