#pragma once

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

#include "mml/core/price_level.hpp"
#include "mml/core/price_levels.hpp"
#include "mml/core/types.hpp"

namespace mml {

// Policy interface for order-id lookup:
//
//   bool        accepts(OrderId) const
//   bool        contains(OrderId) const
//   OrderNode*  find(OrderId) const
//   void        insert(OrderId, OrderNode*)
//   void        erase(OrderId)
//   std::size_t size() const

// General purpose: any id space. One node allocation per insert.
class HashIndex {
public:
    explicit HashIndex(const BookConfig& cfg) { map_.reserve(cfg.expected_orders); }

    [[nodiscard]] static bool accepts(OrderId id) noexcept { return id != kNoOrder; }
    [[nodiscard]] bool contains(OrderId id) const { return map_.find(id) != map_.end(); }

    [[nodiscard]] OrderNode* find(OrderId id) const {
        auto it = map_.find(id);
        return it == map_.end() ? nullptr : it->second;
    }

    void insert(OrderId id, OrderNode* node) { map_.emplace(id, node); }
    void erase(OrderId id) { map_.erase(id); }
    [[nodiscard]] std::size_t size() const noexcept { return map_.size(); }

private:
    std::unordered_map<OrderId, OrderNode*> map_;
};

// Direct-mapped vector. Assumes ids are small and roughly sequential, which is
// true when the gateway assigns them. A single indexed load per lookup and no
// allocation once the vector has grown past the highest live id.
class DenseIndex {
public:
    static constexpr OrderId kMaxId = OrderId{1} << 32;

    explicit DenseIndex(const BookConfig& cfg) { slots_.reserve(cfg.expected_orders); }

    [[nodiscard]] static bool accepts(OrderId id) noexcept { return id != kNoOrder && id < kMaxId; }
    [[nodiscard]] bool contains(OrderId id) const noexcept { return find(id) != nullptr; }

    [[nodiscard]] OrderNode* find(OrderId id) const noexcept {
        return id < slots_.size() ? slots_[static_cast<std::size_t>(id)] : nullptr;
    }

    void insert(OrderId id, OrderNode* node) {
        const auto i = static_cast<std::size_t>(id);
        if (i >= slots_.size()) {
            slots_.resize(std::max(i + 1, slots_.size() * 2), nullptr);
        }
        slots_[i] = node;
        ++size_;
    }

    void erase(OrderId id) noexcept {
        slots_[static_cast<std::size_t>(id)] = nullptr;
        --size_;
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    std::vector<OrderNode*> slots_;
    std::size_t size_{0};
};

}  // namespace mml
