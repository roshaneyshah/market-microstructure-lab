#pragma once

#include <cstdint>
#include <vector>

#include "mml/core/types.hpp"
#include "mml/engine/order_gateway.hpp"
#include "mml/sim/random.hpp"

namespace mml {

// Two-state Markov regime switching for the latent fundamental price.
struct RegimeConfig {
    double sigma_calm{0.35};       // fundamental volatility, ticks per sqrt(step)
    double sigma_volatile{1.1};
    double p_calm_to_volatile{0.002};
    double p_volatile_to_calm{0.02};
    double volatile_market_rate_multiplier{1.6};
};

// Parameters of the synthetic "background" order flow.
//
// A latent fundamental value follows a regime-switching random walk. Liquidity
// providers post limit orders a geometric number of ticks away from the
// fundamental; liquidity takers send market orders whose direction is tilted
// toward the fundamental (partially informed flow). Resting orders are
// cancelled at a constant per-order hazard rate. Prices discover the
// fundamental through the book, so spread, depth and impact are emergent
// rather than imposed.
struct FlowConfig {
    Price initial_price{10'000};
    double limit_rate{6.0};           // expected limit orders per step
    double market_rate{0.8};          // expected market orders per step
    double cancel_rate{0.05};         // cancellation hazard per resting order per step
    double min_offset{0.5};           // minimum distance of new quotes from fundamental, ticks
    double depth_decay{0.3};          // geometric parameter for extra distance (smaller = deeper book)
    Quantity limit_size_min{1};
    Quantity limit_size_max{6};
    Quantity market_size_min{1};
    Quantity market_size_max{12};
    double informed_strength{0.6};    // 0 = uninformed takers, 1 = fully tilted to fundamental
    double informed_scale{2.0};       // ticks of mispricing for the tilt to saturate
    double permanent_impact{0.01};    // fundamental move per unit of net aggressive volume (Kyle lambda)
    double seasonality{0.0};          // amplitude of U-shaped intraday activity (0 = flat, < 3)
    std::size_t initial_levels{15};
    Quantity initial_quantity_per_level{20};
    RegimeConfig regime{};
};

// Generates and submits the background flow through a gateway.
class BackgroundFlow {
public:
    BackgroundFlow(const FlowConfig& cfg, OrderGateway& gateway, std::uint64_t seed);

    // Place symmetric initial liquidity around the initial price.
    void seed_book();

    // Regime transition, one random-walk step of the fundamental, and the
    // permanent impact of the net aggressive volume traded since the last call
    // (by every participant, strategies included).
    void update_fundamental();

    // Generate and process one step of arrivals. `activity` scales arrival
    // rates (intraday seasonality).
    void step(double activity);

    [[nodiscard]] double fundamental() const noexcept { return fundamental_; }
    [[nodiscard]] int regime() const noexcept { return regime_; }
    [[nodiscard]] std::size_t live_orders() const noexcept { return live_.size(); }
    [[nodiscard]] TraderId trader() const noexcept { return trader_; }

private:
    void place_limit();
    void place_market();
    void cancel_random();
    [[nodiscard]] Price clamp_price(Price p) const noexcept;

    FlowConfig cfg_;
    OrderGateway& gateway_;
    Rng rng_;
    TraderId trader_;
    double fundamental_;
    int regime_{0};
    Quantity last_signed_volume_{0};
    std::vector<OrderId> live_;
    std::vector<std::uint8_t> events_;
};

}  // namespace mml
