#pragma once

#include <cstdint>
#include <vector>

#include "mml/core/order_book.hpp"
#include "mml/engine/matching_engine.hpp"
#include "mml/engine/order_gateway.hpp"
#include "mml/sim/order_flow.hpp"

namespace mml {

struct SimConfig {
    FlowConfig flow{};
    BookConfig book{};
    std::size_t warmup_steps{300};  // background only, not recorded
    std::size_t steps{2'000};       // recorded session during which agents act
    std::uint64_t seed{1};
};

// State of the market at the end of each recorded step.
struct StepRecord {
    double mid{0.0};
    Price best_bid{0};  // 0 if the side is empty
    Price best_ask{0};
    Quantity bid_top{0};
    Quantity ask_top{0};
    Quantity volume{0};  // traded during this step, all participants
    double fundamental{0.0};
    int regime{0};
};

struct SimContext {
    OrderGateway& gateway;
    const OrderBook& book;
    TraderId self;
    std::size_t step;   // 0-based index within the recorded session
    std::size_t steps;  // session length
    double mid;         // reference mid at the start of the step
    Quantity last_step_volume;
    const std::vector<StepRecord>& history;
};

// A strategy participating in the simulation. Agents act once per step,
// before that step's background flow, and receive fills asynchronously
// through on_fill while the background flow trades against their orders.
class Agent : public FillListener {
public:
    ~Agent() override = default;
    virtual void on_start(SimContext& /*ctx*/) {}
    virtual void on_step(SimContext& ctx) = 0;
    virtual void on_finish(SimContext& /*ctx*/) {}
    void on_fill(const Fill& /*fill*/) override {}

    [[nodiscard]] TraderId trader_id() const noexcept { return trader_id_; }

private:
    friend class MarketSimulator;
    TraderId trader_id_{kNoTrader};
};

// Discrete-time event simulator: each step advances the fundamental, lets
// agents act, then processes a randomly interleaved batch of background limit
// orders, market orders and cancellations.
class MarketSimulator {
public:
    explicit MarketSimulator(const SimConfig& cfg);

    MarketSimulator(const MarketSimulator&) = delete;
    MarketSimulator& operator=(const MarketSimulator&) = delete;

    // The agent must outlive the simulator run.
    void add_agent(Agent& agent);

    void run();

    [[nodiscard]] const std::vector<StepRecord>& history() const noexcept { return history_; }
    [[nodiscard]] const MatchingEngine& engine() const noexcept { return engine_; }
    [[nodiscard]] OrderGateway& gateway() noexcept { return gateway_; }
    [[nodiscard]] const SimConfig& config() const noexcept { return cfg_; }

    // Arrival-rate multiplier for recorded step `step`.
    [[nodiscard]] double activity(std::size_t step) const noexcept;

    // U-shaped intraday profile on x in [0, 1] with mean 1: 1 + a((2x-1)^2 - 1/3).
    [[nodiscard]] static double seasonality(double x, double amplitude) noexcept;

    // Normalised expected-volume weights for steps [start, start + n).
    [[nodiscard]] std::vector<double> expected_volume_profile(std::size_t start, std::size_t n) const;

private:
    SimContext context(Agent& agent, std::size_t step);
    void record();

    SimConfig cfg_;
    MatchingEngine engine_;
    OrderGateway gateway_;
    BackgroundFlow flow_;
    std::vector<Agent*> agents_;
    std::vector<StepRecord> history_;
    Quantity last_step_volume_{0};
};

}  // namespace mml
