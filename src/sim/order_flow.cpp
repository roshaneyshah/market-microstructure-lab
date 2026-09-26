#include "mml/sim/order_flow.hpp"

#include <algorithm>
#include <cmath>

namespace mml {

namespace {
enum Event : std::uint8_t { kLimit, kMarket, kCancel };
}

BackgroundFlow::BackgroundFlow(const FlowConfig& cfg, OrderGateway& gateway, std::uint64_t seed)
    : cfg_(cfg),
      gateway_(gateway),
      rng_(seed),
      trader_(gateway.register_trader(nullptr)),
      fundamental_(static_cast<double>(cfg.initial_price)) {
    gateway_.set_reference_mid(fundamental_);
}

void BackgroundFlow::seed_book() {
    const Price p0 = cfg_.initial_price;
    for (std::size_t i = 1; i <= cfg_.initial_levels; ++i) {
        const auto d = static_cast<Price>(i);
        for (Side side : {Side::Buy, Side::Sell}) {
            const Price px = clamp_price(side == Side::Buy ? p0 - d : p0 + d);
            const auto r = gateway_.limit(trader_, side, px, cfg_.initial_quantity_per_level);
            if (r.result.resting > 0) {
                live_.push_back(r.id);
            }
        }
    }
}

void BackgroundFlow::update_fundamental() {
    const RegimeConfig& rc = cfg_.regime;
    if (regime_ == 0 && rng_.bernoulli(rc.p_calm_to_volatile)) {
        regime_ = 1;
    } else if (regime_ == 1 && rng_.bernoulli(rc.p_volatile_to_calm)) {
        regime_ = 0;
    }
    const double sigma = regime_ == 0 ? rc.sigma_calm : rc.sigma_volatile;
    const Quantity signed_volume = gateway_.engine().stats().signed_volume;
    const auto net_flow = static_cast<double>(signed_volume - last_signed_volume_);
    last_signed_volume_ = signed_volume;
    fundamental_ += sigma * rng_.normal() + cfg_.permanent_impact * net_flow;
    fundamental_ = std::max(fundamental_, 2.0 + cfg_.min_offset);
}

void BackgroundFlow::step(double activity) {
    const double market_mult = regime_ == 1 ? cfg_.regime.volatile_market_rate_multiplier : 1.0;
    const auto n_limit = rng_.poisson(cfg_.limit_rate * activity);
    const auto n_market = rng_.poisson(cfg_.market_rate * activity * market_mult);
    const auto n_cancel = rng_.poisson(cfg_.cancel_rate * static_cast<double>(live_.size()));

    events_.clear();
    events_.insert(events_.end(), n_limit, kLimit);
    events_.insert(events_.end(), n_market, kMarket);
    events_.insert(events_.end(), n_cancel, kCancel);
    // Fisher-Yates shuffle so event types interleave randomly within a step.
    for (std::size_t i = events_.size(); i > 1; --i) {
        const auto j = static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(i) - 1));
        std::swap(events_[i - 1], events_[j]);
    }
    for (std::uint8_t e : events_) {
        switch (e) {
            case kLimit: place_limit(); break;
            case kMarket: place_market(); break;
            default: cancel_random(); break;
        }
    }
}

void BackgroundFlow::place_limit() {
    const Side side = rng_.bernoulli(0.5) ? Side::Buy : Side::Sell;
    const auto extra = static_cast<Price>(rng_.geometric(cfg_.depth_decay));
    const Price px = side == Side::Buy
                         ? static_cast<Price>(std::floor(fundamental_ - cfg_.min_offset)) - extra
                         : static_cast<Price>(std::ceil(fundamental_ + cfg_.min_offset)) + extra;
    const Quantity qty = rng_.uniform_int(cfg_.limit_size_min, cfg_.limit_size_max);
    const auto r = gateway_.limit(trader_, side, clamp_price(px), qty);
    if (r.result.resting > 0) {
        live_.push_back(r.id);
    }
}

void BackgroundFlow::place_market() {
    const double mispricing = fundamental_ - gateway_.reference_mid();
    const double p_buy =
        0.5 + 0.5 * cfg_.informed_strength * std::tanh(mispricing / std::max(cfg_.informed_scale, 1e-9));
    const Side side = rng_.bernoulli(p_buy) ? Side::Buy : Side::Sell;
    const Quantity qty = rng_.uniform_int(cfg_.market_size_min, cfg_.market_size_max);
    gateway_.market(trader_, side, qty);
}

void BackgroundFlow::cancel_random() {
    if (live_.empty()) {
        return;
    }
    const auto i = static_cast<std::size_t>(rng_.uniform_int(0, static_cast<std::int64_t>(live_.size()) - 1));
    // Returns false if the order has already been filled; either way the id
    // leaves the live list.
    gateway_.cancel(trader_, live_[i]);
    live_[i] = live_.back();
    live_.pop_back();
}

Price BackgroundFlow::clamp_price(Price p) const noexcept { return std::max<Price>(p, 1); }

}  // namespace mml
