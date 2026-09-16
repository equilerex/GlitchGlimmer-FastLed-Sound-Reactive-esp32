#pragma once

#include <cstddef>
#include <iterator>

// Fixed-capacity ring buffer, the storage behind the per-frame history
// containers.
//
// std::deque was the loop's only throw path. It takes and returns a node block
// every few frames (456 bytes for MoodSnapshot, 480 for AudioSnapshot), and
// main.ino calls controller.update() bare inside loop(), so a push_back that
// fails throws std::bad_alloc with nothing to catch it, reaches
// std::terminate, and reboots the board. Both histories are fixed-capacity by
// construction, so the block-allocating container buys nothing. Here the
// capacity is a compile-time constant and the storage is one block, reserved
// when the object is constructed. push_back never allocates.
//
// Indexing is oldest-first, matching std::deque: operator[](0) is the oldest
// retained element and operator[](size() - 1) is the newest.
template <typename T, std::size_t Capacity>
class SnapshotRing {
    static_assert(Capacity > 0, "SnapshotRing needs a nonzero capacity");

public:
    using value_type            = T;
    using size_type             = std::size_t;
    using difference_type       = std::ptrdiff_t;

    // One iterator type, const. Every call site reads a history through a const
    // reference, so a mutable iterator would be unreachable.
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

        const_iterator() = default;
        const_iterator(const SnapshotRing* ring, size_type pos) : ring_(ring), pos_(pos) {}

        reference operator*() const  { return (*ring_)[pos_]; }
        pointer   operator->() const { return &(*ring_)[pos_]; }
        reference operator[](difference_type n) const { return (*ring_)[static_cast<size_type>(static_cast<difference_type>(pos_) + n)]; }

        const_iterator& operator++()    { ++pos_; return *this; }
        const_iterator& operator--()    { --pos_; return *this; }
        const_iterator& operator+=(difference_type n) { pos_ = static_cast<size_type>(static_cast<difference_type>(pos_) + n); return *this; }
        const_iterator& operator-=(difference_type n) { return *this += -n; }

        const_iterator operator++(int) { const_iterator t = *this; ++pos_; return t; }
        const_iterator operator--(int) { const_iterator t = *this; --pos_; return t; }

        friend const_iterator operator+(const_iterator it, difference_type n) { return it += n; }
        friend const_iterator operator+(difference_type n, const_iterator it) { return it += n; }
        friend const_iterator operator-(const_iterator it, difference_type n) { return it -= n; }
        friend difference_type operator-(const const_iterator& a, const const_iterator& b) {
            return static_cast<difference_type>(a.pos_) - static_cast<difference_type>(b.pos_);
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.pos_ == b.pos_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.pos_ != b.pos_; }
        friend bool operator< (const const_iterator& a, const const_iterator& b) { return a.pos_ <  b.pos_; }
        friend bool operator> (const const_iterator& a, const const_iterator& b) { return a.pos_ >  b.pos_; }
        friend bool operator<=(const const_iterator& a, const const_iterator& b) { return a.pos_ <= b.pos_; }
        friend bool operator>=(const const_iterator& a, const const_iterator& b) { return a.pos_ >= b.pos_; }

    private:
        const SnapshotRing* ring_ = nullptr;
        size_type           pos_  = 0;
    };

    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    SnapshotRing() = default;
    SnapshotRing(const SnapshotRing&)            = delete;
    SnapshotRing& operator=(const SnapshotRing&) = delete;

    static constexpr size_type capacity() { return Capacity; }

    size_type size()  const { return count_; }
    bool      empty() const { return count_ == 0; }

    const T& operator[](size_type i) const { return buf_[(head_ + i) % Capacity]; }
    const T& front() const { return buf_[head_]; }
    const T& back()  const { return buf_[(head_ + count_ + Capacity - 1) % Capacity]; }

    void push_back(const T& value) {
        buf_[(head_ + count_) % Capacity] = value;
        if (count_ < Capacity) {
            ++count_;
        } else {
            head_ = (head_ + 1) % Capacity;
        }
    }

    void pop_front() {
        if (count_ == 0) return;
        head_ = (head_ + 1) % Capacity;
        --count_;
    }

    void clear() { head_ = 0; count_ = 0; }

    const_iterator          begin()  const { return const_iterator(this, 0); }
    const_iterator          end()    const { return const_iterator(this, count_); }
    const_reverse_iterator  rbegin() const { return const_reverse_iterator(end()); }
    const_reverse_iterator  rend()   const { return const_reverse_iterator(begin()); }

private:
    T         buf_[Capacity];
    size_type head_  = 0;
    size_type count_ = 0;
};
