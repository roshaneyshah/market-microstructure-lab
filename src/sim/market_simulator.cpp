#include "mml/sim/market_simulator.hpp"

#include <algorithm>
#include <numeric>

namespace mml {

MarketSimulator::MarketSimulator(const SimConfig& cfg)
    : cfg_(cfg), engine_(cfg.book), gateway_(engine_), flow_(cfg.flow, gateway_, cfg.seed) {
    history_.reserve(cfg.steps);
}

void MarketSimulator::add_agent(Agent& agent) {
    agent.trader_id_ = gateway_.register_trader(&agent);
    agents_.push_back(&agent);
}

double MarketSimulator::seasonality(double x, double amplitude) noexcept {
    const double c = 2.0 * x - 1.0;
    return std::max(1e-3, 1.0 + amplitude * (c * c - 1.0 / 3.0));
}

double MarketSimulator::activity(std::size_t step) const noexcept {
    if (cfg_.steps == 0) {
        return 1.0;
    }
    const double x = (static_cast<double>(step) + 0.5) / static_cast<double>(cfg_.steps);
    return seasonality(x, cfg_.flow.seasonality);
}

std::vector<double> MarketSimulator::expected_volume_profile(std::size_t start, std::size_t n) const {
    std::vector<double> w(n);
    for (std::size_t i = 0; i < n; ++i) {
        w[i] = activity(start + i);
    }
    const double total = std::accumulate(w.begin(), w.end(), 0.0);
    if (total > 0.0) {
        for (double& v : w) {
            v /= total;
        }
    }
    return w;
}

SimContext MarketSimulator::context(Agent& agent, std::size_t step) {
    return SimContext{gateway_,          engine_.book(),     agent.trader_id(), step, cfg_.steps,
                      gateway_.reference_mid(), last_step_volume_, history_};
}

void MarketSimulator::run() {
    flow_.seed_book();
    for (std::size_t w = 0; w < cfg_.warmup_steps; ++w) {
        flow_.update_fundamental();
        const Quantity before = engine_.stats().volume;
        flow_.step(1.0);
        last_step_volume_ = engine_.stats().volume - before;
    }

    for (Agent* a : agents_) {
        SimContext ctx = context(*a, 0);
        a->on_start(ctx);
    }

    for (std::size_t s = 0; s < cfg_.steps; ++s) {
        flow_.update_fundamental();
        const Quantity before = engine_.stats().volume;
        for (Agent* a : agents_) {
            SimContext ctx = context(*a, s);
            a->on_step(ctx);
        }
        flow_.step(activity(s));
        last_step_volume_ = engine_.stats().volume - before;
        record();
    }

    for (Agent* a : agents_) {
        SimContext ctx = context(*a, cfg_.steps);
        a->on_finish(ctx);
    }
}

void MarketSimulator::record() {
    const OrderBook& book = engine_.book();
    StepRecord r;
    r.mid = gateway_.reference_mid();
    r.best_bid = book.best_bid().value_or(0);
    r.best_ask = book.best_ask().value_or(0);
    r.bid_top = book.top_quantity(Side::Buy);
    r.ask_top = book.top_quantity(Side::Sell);
    r.volume = last_step_volume_;
    r.fundamental = flow_.fundamental();
    r.regime = flow_.regime();
    history_.push_back(r);
}

}  // namespace mml
