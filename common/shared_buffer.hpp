#ifndef SHARED_BUFFER_HPP
#define SHARED_BUFFER_HPP

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace bip = boost::interprocess;

template <std::size_t Capacity> struct SharedBuffer
{

    SharedBuffer(const std::string &shm_name, bool create) : m_shm_name(shm_name), m_creator(create)
    {
        if (create)
        {
            bip::shared_memory_object::remove(shm_name.c_str());
            // Buffer size + allacator size
            const std::size_t shm_size = sizeof(Buffer) + 64 * 1024;
            m_shm = bip::managed_shared_memory(bip::create_only, shm_name.c_str(), shm_size);
            m_buf = m_shm.construct<Buffer>("buffer")();
        }
        else
        {
            m_shm = bip::managed_shared_memory(bip::open_only, shm_name.c_str());
            m_buf = m_shm.find<Buffer>("buffer").first;
        }
        start_segment_ptr = m_shm.get_address();
    }

    ~SharedBuffer()
    {
        if (m_creator)
        {
            bip::shared_memory_object::remove(m_shm_name.c_str());
        }
    }

    bool produce_block(std::span<const uint8_t> src)
    {
        if (m_buf == nullptr)
        {
            return false;
        }
        bip::scoped_lock lock(m_buf->mutex);
        m_buf->not_full.wait(lock, [&] { return m_buf->available_space() >= src.size() || m_buf->closed; });

        if (!(m_buf->available_space() >= src.size()))
        {
            return false;
        }

        std::size_t first_chunk = std::min(src.size(), Capacity - m_buf->head);
        std::copy(src.begin(), src.begin() + first_chunk, m_buf->data + m_buf->head);

        std::size_t remaining = src.size() - first_chunk;
        if (remaining > 0)
        {
            std::copy(src.begin() + first_chunk, src.end(), m_buf->data);
        }

        m_buf->head = (m_buf->head + src.size()) % Capacity;
        m_buf->count += src.size();

        m_buf->not_empty.notify_all();
        return true;
    }

    bool consume_block(std::span<uint8_t> dest)
    {
        if (m_buf == nullptr)
        {
            return false;
        }
        bip::scoped_lock lock(m_buf->mutex);
        m_buf->not_empty.wait(lock, [&] { return m_buf->count >= dest.size() || m_buf->closed; });

        if (!(m_buf->count >= dest.size()))
        {
            return false;
        }

        std::size_t first_chunk = std::min(dest.size(), Capacity - m_buf->tail);
        std::copy(m_buf->data + m_buf->tail, m_buf->data + m_buf->tail + first_chunk, dest.begin());

        std::size_t remaining = dest.size() - first_chunk;
        if (remaining > 0)
        {
            std::copy(m_buf->data, m_buf->data + remaining, dest.begin() + first_chunk);
        }

        m_buf->tail = (m_buf->tail + dest.size()) % Capacity;
        m_buf->count -= dest.size();

        m_buf->not_full.notify_all();
        return true;
    }

    void close()
    {
        if (m_buf == nullptr)
        {
            return;
        }
        bip::scoped_lock lock(m_buf->mutex);
        m_buf->closed = true;
        m_buf->not_empty.notify_all();
        m_buf->not_full.notify_all();
    }

    void reset()
    {
        if (m_buf == nullptr)
        {
            return;
        }
        bip::scoped_lock lock(m_buf->mutex);
        m_buf->closed = false;
        m_buf->head = 0;
        m_buf->tail = 0;
        m_buf->count = 0;
    }

    inline void* get_sharred_buffer_pointer()
    {
        return start_segment_ptr;
    }

  private:
    struct Buffer
    {
        bip::interprocess_mutex mutex;
        bip::interprocess_condition not_empty;
        bip::interprocess_condition not_full;

        std::size_t head = 0;
        std::size_t tail = 0;
        std::size_t count = 0;
        bool closed = false;

        uint8_t data[Capacity];

        Buffer() = default;

        size_t available_space() const
        {
            return Capacity - count;
        }
    };

    std::string m_shm_name;
    bip::managed_shared_memory m_shm;
    Buffer *m_buf;
    void *start_segment_ptr = nullptr;
    const bool m_creator = false;
};

#endif