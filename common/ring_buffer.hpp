#ifndef RING_BUFFER_HPP
#define RING_BUFFER_HPP

#include <condition_variable>
#include <mutex>

template <typename T, std::size_t Capacity> class RingBuffer
{
  public:
    struct Buffer
    {
        std::mutex mutex;
        std::condition_variable not_empty;
        std::condition_variable not_full;

        std::size_t head = 0;
        std::size_t tail = 0;
        std::size_t count = 0;

        T data[Capacity];
    };

    RingBuffer() = default;
    ~RingBuffer() = default;
};

#endif // RING_BUFFER_HPP
