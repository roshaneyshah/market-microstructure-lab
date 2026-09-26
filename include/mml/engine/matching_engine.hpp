#pragma once

#include <cstdint>
#include <vector>

#include "mml/core/order.hpp"
#include "mml/core/order_book.hpp"

namespace mml {

struct MarketStats {
    Quantity volume{0};
    Quantity signed_volume{0};  // buyer-initiated minus seller-initiated volume
    double notional{0.0};  // sum of price * quantity, in ticks
    std::uint64_t trades{0};
    std::uint64_t orders{0};
    std::uint64_t cancels{0};
    std::uint64_t rejects{0};
};

// Owns the book for one instrument. Stamps every message with a monotonically
// increasing sequence number (the exchange clock), records the trade tape for
// the latest message, and keeps running market statistics.
class MatchingEngine {
public:
    explicit MatchingEngine(const BookConfig& cfg = {});

    ExecResult submit(OrderRequest req);
    bool cancel(OrderId id);
    ExecResult modify(OrderId id, Price new_price, Quantity new_quantity);

    [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
    [[nodiscard]] const std::vector<Trade>& last_trades() const noexcept { return last_trades_; }
    [[nodiscard]] const MarketStats& stats() const noexcept { return stats_; }
    [[nodiscard]] Timestamp clock() const noexcept { return clock_; }

    // Optional full tape of every trade (off by default to save memory).
    void keep_tape(bool on) noexcept { keep_tape_ = on; }
    [[nodiscard]] const std::vector<Trade>& tape() const noexcept { return tape_; }

private:
    void record(const ExecResult& r);

    OrderBook book_;
    std::vector<Trade> last_trades_;
    std::vector<Trade> tape_;
    MarketStats stats_;
    Timestamp clock_{0};
    bool keep_tape_{false};
};

}  // namespace mml
