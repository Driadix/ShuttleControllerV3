// Static fixed-capacity queue for the proving slice.
// Rule R1 (issue #51 section 6.1): no dynamic allocation anywhere in domain.
#pragma once

#include <cstddef>
#include <cstdint>

namespace slice
{

template <typename T, std::size_t N> class StaticQueue
{
  public:
    using size_type = std::size_t;

    StaticQueue() = default;

    // Never blocks. Returns false on full (caller applies class overload policy).
    bool push(const T& value)
    {
        if (m_size >= N)
        {
            ++m_overflow;
            return false;
        }
        m_items[(m_head + m_size) % N] = value;
        ++m_size;
        return true;
    }

    bool pop(T& out)
    {
        if (m_size == 0)
        {
            return false;
        }
        out = m_items[m_head];
        m_head = (m_head + 1) % N;
        --m_size;
        return true;
    }

    // Copy of the head element without removing it (Sink::tick decision
    // support, observability #72; dev-only use, R1-safe).
    bool peek(T& out) const
    {
        if (m_size == 0)
        {
            return false;
        }
        out = m_items[m_head];
        return true;
    }

    bool empty() const { return m_size == 0; }
    bool full() const { return m_size >= N; }
    size_type size() const { return m_size; }
    constexpr size_type capacity() const { return N; }

    // Overflow counter - observable by the measurement recorder (obligation #7).
    std::uint32_t overflow_count() const { return m_overflow; }
    void reset_overflow() { m_overflow = 0; }

  private:
    T m_items[N] = {};
    size_type m_head = 0;
    size_type m_size = 0;
    std::uint32_t m_overflow = 0;
};

} // namespace slice
