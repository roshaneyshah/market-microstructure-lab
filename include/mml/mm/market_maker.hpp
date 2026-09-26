#pragma once

#include <optional>
#include <string>
#include <vector>

#include "mml/mm/signals.hpp"
#include "mml/portfolio/portfolio.hpp"
#include "mml/sim/market_simulator.hpp"

namespace mml {

struct Quote {
    Price price{0};
    Quantity quantity{0};
};

struct QuotePair {
    std::optional<Quote> bid;
    std::optional<Quote> ask;
};

struct FillRecord {
    std::size_t step{0};
    Side side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    double mid{0.0};  // reference mid when the fill happened
    bool maker{true};
};

// Base class for quoting strategies. Once per step it marks the portfolio,
// updates order-flow signals, asks the strategy for a two-sided quote, and
// reconciles live orders with it. An unchanged price keeps its order (and
// therefore its queue position); a changed price is cancelled and replaced.
// Quotes are post-only: they are clamped so they never cross the book.
class MarketMaker : public Agent {
public:
    MarketMaker(std::string name, FeeSchedule fees);

    void on_step(SimContext& ctx) final;
    void on_finish(SimContext& ctx) override;
    void on_fill(const Fill& fill) override;

    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] const Portfolio& portfolio() const noexcept { return portfolio_; }
    [[nodiscard]] const std::vector<FillRecord>& fills() const noexcept { return fills_; }
    // Mark-to-mid total PnL and position at the start of each step.
    [[nodiscard]] const std::vector<double>& pnl_path() const noexcept { return pnl_path_; }
    [[nodiscard]] const std::vector<Quantity>& inventory_path() const noexcept { return inventory_path_; }
    [[nodiscard]] std::size_t quote_updates() const noexcept { return quote_updates_; }

protected:
    [[nodiscard]] virtual QuotePair compute_quotes(const SimContext& ctx) = 0;
    [[nodiscard]] const OrderFlowSignals& signals() const noexcept { return signals_; }

    // Helpers for subclasses: floor/ceil of a fractional price.
    [[nodiscard]] static Price bid_price(double x) noexcept;
    [[nodiscard]] static Price ask_price(double x) noexcept;

private:
    struct LiveOrder {
        OrderId id{kNoOrder};
        Price price{0};
        Quantity remaining{0};
    };

    void reconcile(SimContext& ctx, Side side, std::optional<Quote> want, LiveOrder& live);

    std::string name_;
    Portfolio portfolio_;
    OrderFlowSignals signals_;
    LiveOrder bid_;
    LiveOrder ask_;
    std::size_t step_{0};
    std::size_t quote_updates_{0};
    std::vector<FillRecord> fills_;
    std::vector<double> pnl_path_;
    std::vector<Quantity> inventory_path_;
};

// Quotes a fixed half-spread around the mid, ignoring inventory.
struct SymmetricParams {
    double half_spread{0.5};  // 0.5 on a 1-tick spread means joining the touch
    Quantity size{5};
};

class SymmetricMarketMaker final : public MarketMaker {
public:
    explicit SymmetricMarketMaker(SymmetricParams p, FeeSchedule fees = {}, std::string name = "Symmetric");

protected:
    [[nodiscard]] QuotePair compute_quotes(const SimContext& ctx) override;

private:
    SymmetricParams p_;
};

// Shifts both quotes against inventory (linear skew) and stops quoting the
// side that would breach the inventory limit.
struct InventoryParams {
    double half_spread{0.5};
    Quantity size{5};
    double skew_per_unit{0.05};  // ticks of quote shift per unit of inventory
    Quantity max_inventory{60};
};

class InventoryAwareMarketMaker final : public MarketMaker {
public:
    explicit InventoryAwareMarketMaker(InventoryParams p, FeeSchedule fees = {}, std::string name = "InventorySkew");

protected:
    [[nodiscard]] QuotePair compute_quotes(const SimContext& ctx) override;

private:
    InventoryParams p_;
};

// Avellaneda and Stoikov (2008), stationary variant:
//   reservation price  r = s - q * gamma * sigma^2 * H
//   total spread       delta = gamma * sigma^2 * H + (2 / gamma) * ln(1 + gamma / k)
// with q the inventory in quote-size lots and H a rolling horizon in steps
// instead of the terminal time T - t. Optionally the reservation price is
// shifted by order-flow signals: r += alpha_imbalance * I + alpha_ofi * OFI_norm.
struct AvellanedaStoikovParams {
    double gamma{0.1};         // risk aversion
    double k{1.5};             // decay of fill intensity with distance, per tick
    double sigma{0.0};         // ticks per sqrt(step); 0 = online EWMA estimate
    double horizon_steps{100.0};
    Quantity size{5};
    Quantity max_inventory{60};
    double min_half_spread{0.5};
    double alpha_imbalance{0.0};
    double alpha_ofi{0.0};
};

class AvellanedaStoikovMarketMaker final : public MarketMaker {
public:
    explicit AvellanedaStoikovMarketMaker(AvellanedaStoikovParams p, FeeSchedule fees = {},
                                          std::string name = "AvellanedaStoikov");

    // Exposed for tests and analysis.
    [[nodiscard]] double reservation_price(double mid, double inventory_lots, double sigma) const noexcept;
    [[nodiscard]] double optimal_spread(double sigma) const noexcept;

protected:
    [[nodiscard]] QuotePair compute_quotes(const SimContext& ctx) override;

private:
    AvellanedaStoikovParams p_;
};

}  // namespace mml
