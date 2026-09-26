#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace mml {

// Chunked free-list pool. Objects are allocated in blocks and recycled, so a
// book in steady state performs no heap allocation for order nodes. Pointers
// stay valid for the lifetime of the pool because chunks never move.
template <class T>
class ObjectPool {
public:
    explicit ObjectPool(std::size_t chunk_size = 4096) : chunk_size_(chunk_size == 0 ? 1 : chunk_size) {}

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) noexcept = default;
    ObjectPool& operator=(ObjectPool&&) noexcept = default;

    [[nodiscard]] T* acquire() {
        if (free_.empty()) {
            grow();
        }
        T* p = free_.back();
        free_.pop_back();
        ++live_;
        return p;
    }

    // Capacity for every pooled object is reserved in grow(), so this never
    // reallocates.
    void release(T* p) noexcept {
        free_.push_back(p);
        --live_;
    }

    void reserve(std::size_t n) {
        while (capacity_ < n) {
            grow();
        }
    }

    [[nodiscard]] std::size_t live() const noexcept { return live_; }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    void grow() {
        chunks_.push_back(std::make_unique<T[]>(chunk_size_));
        T* base = chunks_.back().get();
        free_.reserve(capacity_ + chunk_size_);
        for (std::size_t i = chunk_size_; i-- > 0;) {
            free_.push_back(base + i);
        }
        capacity_ += chunk_size_;
    }

    std::size_t chunk_size_;
    std::size_t capacity_{0};
    std::size_t live_{0};
    std::vector<std::unique_ptr<T[]>> chunks_;
    std::vector<T*> free_;
};

}  // namespace mml
