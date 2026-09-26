#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "mml/core/object_pool.hpp"
#include "mml/core/order.hpp"
#include "mml/core/order_index.hpp"
#include "mml/core/price_level.hpp"
#include "mml/core/price_levels.hpp"
#include "mml/core/types.hpp"

namespace mml {

// Limit order book with price-time priority matching.
//
// `Levels` chooses how price levels are stored (tree or flat ladder) and
// `Index` chooses how order ids are resolved to nodes (hash map or dense
// vector). All four combinations behave identically; they differ only in
// performance, which is what the benchmark suite measures.
//
// Semantics:
//  * Incoming orders match against the opposite side, best price first, and
//    oldest order first within a price. Trades print at the resting price.
//  * Limit GTC remainders rest. IOC and market remainders are cancelled.
//  * FOK orders execute only if the full quantity is available within the
//    limit price; otherwise they are cancelled with no fills.
//  * modify() that only reduces quantity at the same price keeps queue
//    position. Any price change or quantity increase loses priority
//    (cancel and re-add, which may trade if the new price crosses).
//
// The book is single threaded by design. Determinism and exact replay matter
// more here than concurrency; an exchange would shard by instrument instead.
template <template <Side> class Levels, class Index>
class BasicOrderBook {
public:
    explicit BasicOrderBook(const BookConfig& cfg = {})
        : bids_(cfg), asks_(cfg), index_(cfg), pool_(std::max<std::size_t>(cfg.expected_orders / 4, 256)) {}

    BasicOrderBook(const BasicOrderBook&) = delete;
    BasicOrderBook& operator=(const BasicOrderBook&) = delete;

    // Process a new order. Generated trades are appended to `trades`.
    ExecResult add(const OrderRequest& req, std::vector<Trade>& trades) {
        if (!index_.accepts(req.id)) {
            return reject(RejectReason::InvalidOrderId);
        }
        if (req.quantity <= 0) {
            return reject(RejectReason::InvalidQuantity);
        }
        if (req.type == OrderType::Limit && !valid_price(req.side, req.price)) {
            return reject(RejectReason::InvalidPrice);
        }
        if (index_.contains(req.id)) {
            return reject(RejectReason::DuplicateOrderId);
        }
        return req.side == Side::Buy ? execute(req, bids_, asks_, trades) : execute(req, asks_, bids_, trades);
    }

    // Remove a resting order. Returns false if the id is not resting.
    bool cancel(OrderId id) {
        OrderNode* node = index_.find(id);
        if (node == nullptr) {
            return false;
        }
        remove_node(node);
        return true;
    }

    // Change price and/or open quantity of a resting order. `new_quantity` is
    // the new open (remaining) quantity.
    ExecResult modify(OrderId id, Price new_price, Quantity new_quantity, std::vector<Trade>& trades,
                      Timestamp ts = 0) {
        OrderNode* node = index_.find(id);
        if (node == nullptr) {
            return reject(RejectReason::UnknownOrder);
        }
        if (new_quantity <= 0) {
            return reject(RejectReason::InvalidQuantity);
        }
        if (new_price == node->price && new_quantity <= node->remaining) {
            node->level->total -= node->remaining - new_quantity;
            node->remaining = new_quantity;
            return ExecResult{OrderStatus::Accepted, RejectReason::None, 0, new_quantity};
        }
        if (!valid_price(node->side, new_price)) {
            return reject(RejectReason::InvalidPrice);
        }
        OrderRequest req = OrderRequest::limit(id, node->side, new_price, new_quantity, TimeInForce::GTC, node->trader);
        req.ts = ts;
        remove_node(node);
        return add(req, trades);
    }

    // ---- Queries -----------------------------------------------------------

    [[nodiscard]] std::optional<Price> best_bid() const noexcept {
        const PriceLevel* l = bids_.best();
        return l != nullptr ? std::optional<Price>(l->price) : std::nullopt;
    }

    [[nodiscard]] std::optional<Price> best_ask() const noexcept {
        const PriceLevel* l = asks_.best();
        return l != nullptr ? std::optional<Price>(l->price) : std::nullopt;
    }

    [[nodiscard]] std::optional<double> mid() const noexcept {
        const auto b = best_bid();
        const auto a = best_ask();
        if (!b || !a) {
            return std::nullopt;
        }
        return 0.5 * static_cast<double>(*b + *a);
    }

    [[nodiscard]] std::optional<Price> spread() const noexcept {
        const auto b = best_bid();
        const auto a = best_ask();
        if (!b || !a) {
            return std::nullopt;
        }
        return *a - *b;
    }

    // Total quantity at the best level of a side (0 if empty).
    [[nodiscard]] Quantity top_quantity(Side side) const noexcept {
        const PriceLevel* l = side == Side::Buy ? bids_.best() : asks_.best();
        return l != nullptr ? l->total : 0;
    }

    [[nodiscard]] Quantity volume_at(Side side, Price price) const noexcept {
        const PriceLevel* l = side == Side::Buy ? bids_.find(price) : asks_.find(price);
        return l != nullptr ? l->total : 0;
    }

    // Aggregated depth, best level first.
    [[nodiscard]] std::vector<LevelSnapshot> depth(Side side, std::size_t max_levels) const {
        std::vector<LevelSnapshot> out;
        out.reserve(max_levels);
        auto collect = [&](const PriceLevel& l) {
            if (out.size() >= max_levels) {
                return false;
            }
            out.push_back(LevelSnapshot{l.price, l.total, l.count});
            return out.size() < max_levels;
        };
        if (max_levels == 0) {
            return out;
        }
        if (side == Side::Buy) {
            bids_.for_each(collect);
        } else {
            asks_.for_each(collect);
        }
        return out;
    }

    // Sum of quantity over the best `max_levels` levels.
    [[nodiscard]] Quantity depth_quantity(Side side, std::size_t max_levels) const {
        Quantity total = 0;
        std::size_t n = 0;
        auto sum = [&](const PriceLevel& l) {
            total += l.total;
            return ++n < max_levels;
        };
        if (max_levels == 0) {
            return 0;
        }
        if (side == Side::Buy) {
            bids_.for_each(sum);
        } else {
            asks_.for_each(sum);
        }
        return total;
    }

    [[nodiscard]] const OrderNode* find(OrderId id) const { return index_.find(id); }
    [[nodiscard]] std::size_t order_count() const noexcept { return index_.size(); }
    [[nodiscard]] std::size_t level_count(Side side) const noexcept {
        return side == Side::Buy ? bids_.size() : asks_.size();
    }
    [[nodiscard]] bool empty() const noexcept { return index_.size() == 0; }

    // Full structural check used by tests and fuzzing. O(orders).
    [[nodiscard]] bool check_invariants(std::string* why = nullptr) const {
        std::size_t orders = 0;
        bool ok = check_side(Side::Buy, bids_, orders, why) && check_side(Side::Sell, asks_, orders, why);
        if (ok && orders != index_.size()) {
            ok = fail(why, "index size does not match number of resting orders");
        }
        if (ok) {
            const auto b = best_bid();
            const auto a = best_ask();
            if (b && a && *b >= *a) {
                ok = fail(why, "book is crossed");
            }
        }
        return ok;
    }

private:
    static ExecResult reject(RejectReason r) noexcept { return ExecResult{OrderStatus::Rejected, r, 0, 0}; }

    [[nodiscard]] bool valid_price(Side side, Price p) const noexcept {
        return side == Side::Buy ? bids_.valid_price(p) : asks_.valid_price(p);
    }

    [[nodiscard]] static bool crosses(const OrderRequest& taker, Price resting_price) noexcept {
        if (taker.type == OrderType::Market) {
            return true;
        }
        return taker.side == Side::Buy ? resting_price <= taker.price : resting_price >= taker.price;
    }

    template <class Own, class Opp>
    ExecResult execute(const OrderRequest& req, Own& own, Opp& opp, std::vector<Trade>& trades) {
        if (req.tif == TimeInForce::FOK && !can_fill(req, opp)) {
            return ExecResult{OrderStatus::Cancelled, RejectReason::None, 0, 0};
        }
        const Quantity left = match(req, opp, trades);
        const Quantity filled = req.quantity - left;
        if (left == 0) {
            return ExecResult{OrderStatus::Filled, RejectReason::None, filled, 0};
        }
        if (req.type == OrderType::Limit && req.tif == TimeInForce::GTC) {
            rest(req, left, own);
            return ExecResult{filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Accepted, RejectReason::None,
                              filled, left};
        }
        return ExecResult{OrderStatus::Cancelled, RejectReason::None, filled, 0};
    }

    template <class Opp>
    [[nodiscard]] bool can_fill(const OrderRequest& req, const Opp& opp) const {
        Quantity needed = req.quantity;
        opp.for_each([&](const PriceLevel& l) {
            if (!crosses(req, l.price)) {
                return false;
            }
            needed -= l.total;
            return needed > 0;
        });
        return needed <= 0;
    }

    template <class Opp>
    Quantity match(const OrderRequest& taker, Opp& opp, std::vector<Trade>& trades) {
        Quantity qty = taker.quantity;
        while (qty > 0) {
            PriceLevel* level = opp.best();
            if (level == nullptr || !crosses(taker, level->price)) {
                break;
            }
            while (qty > 0 && level->head != nullptr) {
                OrderNode* maker = level->head;
                const Quantity q = std::min(qty, maker->remaining);
                trades.push_back(Trade{maker->id, taker.id, maker->trader, taker.trader, taker.side, level->price, q,
                                       taker.ts});
                qty -= q;
                maker->remaining -= q;
                level->total -= q;
                if (maker->remaining == 0) {
                    level->remove(maker);
                    index_.erase(maker->id);
                    pool_.release(maker);
                }
            }
            if (level->empty()) {
                opp.erase(level);
            }
        }
        return qty;
    }

    template <class Own>
    void rest(const OrderRequest& req, Quantity qty, Own& own) {
        OrderNode* node = pool_.acquire();
        node->id = req.id;
        node->trader = req.trader;
        node->side = req.side;
        node->price = req.price;
        node->remaining = qty;
        node->ts = req.ts;
        own.find_or_create(req.price)->push_back(node);
        index_.insert(req.id, node);
    }

    void remove_node(OrderNode* node) {
        PriceLevel* level = node->level;
        level->remove(node);
        if (level->empty()) {
            if (node->side == Side::Buy) {
                bids_.erase(level);
            } else {
                asks_.erase(level);
            }
        }
        index_.erase(node->id);
        pool_.release(node);
    }

    static bool fail(std::string* why, const char* msg) {
        if (why != nullptr) {
            *why = msg;
        }
        return false;
    }

    template <class L>
    bool check_side(Side side, const L& levels, std::size_t& orders, std::string* why) const {
        bool ok = true;
        bool first = true;
        Price prev = 0;
        std::size_t seen_levels = 0;
        levels.for_each([&](const PriceLevel& l) {
            ++seen_levels;
            if (!first && (side == Side::Buy ? l.price >= prev : l.price <= prev)) {
                ok = fail(why, "levels out of order");
                return false;
            }
            first = false;
            prev = l.price;
            Quantity sum = 0;
            std::uint32_t count = 0;
            const OrderNode* last = nullptr;
            for (const OrderNode* n = l.head; n != nullptr; n = n->next) {
                if (n->prev != last || n->level != &l || n->price != l.price || n->side != side || n->remaining <= 0) {
                    ok = fail(why, "corrupt order node");
                    return false;
                }
                if (index_.find(n->id) != n) {
                    ok = fail(why, "index does not point at resting node");
                    return false;
                }
                sum += n->remaining;
                ++count;
                last = n;
            }
            if (last != l.tail || sum != l.total || count != l.count || count == 0) {
                ok = fail(why, "level aggregates inconsistent");
                return false;
            }
            orders += count;
            return true;
        });
        if (ok && seen_levels != levels.size()) {
            ok = fail(why, "level count mismatch");
        }
        return ok;
    }

    Levels<Side::Buy> bids_;
    Levels<Side::Sell> asks_;
    Index index_;
    ObjectPool<OrderNode> pool_;
};

// Default book: tree levels plus hash index. Works for any price and id space.
using OrderBook = BasicOrderBook<MapLevels, HashIndex>;
using MapDenseOrderBook = BasicOrderBook<MapLevels, DenseIndex>;
using LadderHashOrderBook = BasicOrderBook<LadderLevels, HashIndex>;
// Fastest variant: flat ladder plus dense id index. Requires a bounded tick
// range and gateway-assigned sequential ids.
using LadderOrderBook = BasicOrderBook<LadderLevels, DenseIndex>;

}  // namespace mml
