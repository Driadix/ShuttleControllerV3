#pragma once
#include <atomic>
#include <array>
#include <cstdint>

namespace v3::observability {

enum class DropPolicy { Oldest, Newest };
enum class EnqueueStatus { Success, Dropped, Rejected };

template<typename T, size_t N, DropPolicy P>
class BoundedQueue {
public:
    BoundedQueue() : head(0), tail(0), dropCount(0) {}

    EnqueueStatus enqueue(const T& record) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t next_t = (t + 1) % N;
        
        if (next_t == head.load(std::memory_order_acquire)) {
            if constexpr (P == DropPolicy::Oldest) {
                size_t h = head.load(std::memory_order_relaxed);
                head.store((h + 1) % N, std::memory_order_release);
                dropCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                // Drop-newest: reject
                dropCount.fetch_add(1, std::memory_order_relaxed);
                return EnqueueStatus::Rejected;
            }
        }
        
        buffer[t] = record;
        tail.store(next_t, std::memory_order_release);
        return EnqueueStatus::Success;
    }

    bool dequeue(T& record) {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        
        record = buffer[h];
        head.store((h + 1) % N, std::memory_order_release);
        return true;
    }

    // Non-destructive read for the Sink drain (defer-on-backpressure, #49
    // section 10): a record leaves the queue only AFTER a successful send.
    bool peek(T& record) const {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        record = buffer[h];
        return true;
    }

    bool pop() {
        size_t h = head.load(std::memory_order_relaxed);
        if (h == tail.load(std::memory_order_acquire)) return false;
        head.store((h + 1) % N, std::memory_order_release);
        return true;
    }

    uint16_t getDropCount() const { return dropCount.load(std::memory_order_relaxed); }
    size_t size() const {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t h = head.load(std::memory_order_relaxed);
        return (t >= h) ? (t - h) : (N - h + t);
    }

private:
    std::array<T, N> buffer;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    std::atomic<uint16_t> dropCount;
};

} // namespace v3::observability
