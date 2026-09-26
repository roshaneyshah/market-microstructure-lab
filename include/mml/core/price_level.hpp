#pragma once

#include <cstdint>

#include "mml/core/types.hpp"

namespace mml {

struct PriceLevel;

// A resting order. Nodes are pooled and linked intrusively into their price
// level, so cancelling is O(1) once the node is found through the id index.
struct OrderNode {
    OrderId id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    Price price{0};
    Quantity remaining{0};
    Timestamp ts{0};
    OrderNode* prev{nullptr};
    OrderNode* next{nullptr};
    PriceLevel* level{nullptr};
};

// FIFO queue of orders at one price. Head is the oldest order and therefore
// first in line to trade (time priority within a price).
struct PriceLevel {
    Price price{0};
    Quantity total{0};
    std::uint32_t count{0};
    OrderNode* head{nullptr};
    OrderNode* tail{nullptr};

    [[nodiscard]] bool empty() const noexcept { return count == 0; }

    void push_back(OrderNode* n) noexcept {
        n->level = this;
        n->prev = tail;
        n->next = nullptr;
        if (tail != nullptr) {
            tail->next = n;
        } else {
            head = n;
        }
        tail = n;
        ++count;
        total += n->remaining;
    }

    void remove(OrderNode* n) noexcept {
        if (n->prev != nullptr) {
            n->prev->next = n->next;
        } else {
            head = n->next;
        }
        if (n->next != nullptr) {
            n->next->prev = n->prev;
        } else {
            tail = n->prev;
        }
        --count;
        total -= n->remaining;
        n->prev = nullptr;
        n->next = nullptr;
        n->level = nullptr;
    }
};

struct LevelSnapshot {
    Price price{0};
    Quantity quantity{0};
    std::uint32_t orders{0};

    friend bool operator==(const LevelSnapshot&, const LevelSnapshot&) = default;
};

}  // namespace mml
