#ifndef SHARED_BUFFER_HPP
#define SHARED_BUFFER_HPP

#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

namespace bip = boost::interprocess;

class SharedBuffer
{
public:
    SharedBuffer() = default; 
    ~SharedBuffer();

    bool is_initialized() const
    {
        return m_buf != nullptr;
    }
    void init(const std::string &shm_name, std::size_t capacity, bool create);
    bool produce_block(std::span<const uint8_t> src);
    bool consume_block(std::span<uint8_t> dest);

    void close();
    void reset();

    inline void* get_shared_buffer_pointer() { return start_segment_ptr; }

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

        std::size_t capacity = 0;
        bip::offset_ptr<uint8_t> data;

        size_t available_space() const { return capacity - count; }
    };

    std::string m_shm_name;
    bip::managed_shared_memory m_shm;
    Buffer* m_buf = nullptr;
    void* start_segment_ptr = nullptr;
    bool m_creator;
};

#endif