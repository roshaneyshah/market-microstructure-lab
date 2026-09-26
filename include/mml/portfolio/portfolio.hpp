#pragma once

#include <cstdint>

#include "mml/core/types.hpp"

namespace mml {

// Per-unit fees in ticks. Negative values are rebates.
struct FeeSchedule {
    double maker_fee{0.0};
    double taker_fee{0.0};
};

// Decomposition of mark-to-mid PnL.
//
//   spread_capture  sum over fills of side * (mid_at_fill - fill_price) * qty
//   inventory       sum over intervals of position * (change in mid)
//   fees            fees paid (negative when rebates exceed fees)
//
// total() equals cash + position * mid exactly, so the attribution has no
// residual term.
struct PnlAttribution {
    double spread_capture{0.0};
    double inventory{0.0};
    double fees{0.0};

    [[nodiscard]] double total() const noexcept { return spread_capture + inventory - fees; }
};

struct TradingStats {
    std::uint64_t fills{0};
    std::uint64_t maker_fills{0};
    Quantity volume{0};
    Quantity max_abs_position{0};
};

// Position, cash and PnL for one participant, in ticks.
class Portfolio {
public:
    explicit Portfolio(FeeSchedule fees = {}) : fees_(fees) {}

    // `mid` is the reference mid in force when the fill happened.
    void on_fill(Side side, Price price, Quantity qty, bool maker, double mid);

    // Mark inventory to a new mid.
    void mark(double mid);

    [[nodiscard]] Quantity position() const noexcept { return position_; }
    [[nodiscard]] double cash() const noexcept { return cash_; }
    [[nodiscard]] double last_mid() const noexcept { return last_mid_; }
    [[nodiscard]] double average_cost() const noexcept { return avg_cost_; }

    // Mark-to-mid equity relative to a flat start (includes fees).
    [[nodiscard]] double total_pnl() const noexcept {
        return cash_ + static_cast<double>(position_) * last_mid_;
    }
    // Average-cost realized PnL, before fees.
    [[nodiscard]] double realized_pnl() const noexcept { return realized_; }
    [[nodiscard]] double unrealized_pnl() const noexcept {
        return static_cast<double>(position_) * (last_mid_ - avg_cost_);
    }

    [[nodiscard]] const PnlAttribution& attribution() const noexcept { return attribution_; }
    [[nodiscard]] const TradingStats& stats() const noexcept { return stats_; }
    [[nodiscard]] const FeeSchedule& fees() const noexcept { return fees_; }

private:
    void update_cost_basis(int s, Price price, Quantity qty);

    FeeSchedule fees_;
    Quantity position_{0};
    double cash_{0.0};
    double last_mid_{0.0};
    bool has_mid_{false};
    double avg_cost_{0.0};
    double realized_{0.0};
    PnlAttribution attribution_;
    TradingStats stats_;
};

}  // namespace mml
