#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mml/exec/algorithms.hpp"
#include "mml/sim/market_simulator.hpp"

namespace mml {

struct ParentOrder {
    Side side{Side::Buy};
    Quantity quantity{0};
    std::size_t start_step{0};
    std::size_t horizon{1};
};

// Execution quality of one parent order. Costs are signed so that positive
// always means worse for the trader, whatever the side.
struct ExecutionReport {
    std::string algorithm;
    Side side{Side::Buy};
    Quantity target{0};
    Quantity filled{0};
    std::size_t child_orders{0};
    double arrival_mid{0.0};   // decision price
    double average_price{0.0};
    double end_mid{0.0};       // mid when the horizon ends
    double post_mid{0.0};      // mid at the end of the simulation (after decay)
    double market_vwap{0.0};   // VWAP of other participants over the horizon

    // Implementation shortfall vs arrival mid, including opportunity cost of
    // any unfilled quantity marked at end_mid, in basis points of notional.
    double shortfall_bps{0.0};
    // Same quantity in ticks per unit of parent order.
    double shortfall_ticks{0.0};
    // side * (average price - market VWAP), basis points.
    double slippage_vs_vwap_bps{0.0};
    double fill_rate{0.0};
    // Execution cost of the filled part only: side * (avg - arrival) * filled, ticks.
    double execution_cost{0.0};
    // side * (end_mid - arrival_mid), ticks: price move during the execution.
    double market_impact{0.0};
    // side * (post_mid - arrival_mid), ticks: impact left after the horizon.
    double permanent_impact{0.0};
};

// Works a parent order through the gateway using an ExecutionAlgorithm to
// choose child order sizes, one decision per step.
class ExecutionAgent final : public Agent {
public:
    ExecutionAgent(ParentOrder parent, std::unique_ptr<ExecutionAlgorithm> algo);

    void on_step(SimContext& ctx) override;
    void on_finish(SimContext& ctx) override;
    void on_fill(const Fill& fill) override;

    [[nodiscard]] ExecutionReport report() const;
    [[nodiscard]] const ExecutionAlgorithm& algorithm() const noexcept { return *algo_; }
    // Cumulative filled quantity at the end of each horizon step.
    [[nodiscard]] const std::vector<Quantity>& fill_path() const noexcept { return fill_path_; }

private:
    ParentOrder parent_;
    std::unique_ptr<ExecutionAlgorithm> algo_;
    bool started_{false};
    bool ended_{false};
    Quantity filled_{0};
    double notional_{0.0};
    Quantity own_volume_step_{0};
    Quantity own_volume_total_{0};
    double own_notional_total_{0.0};
    std::size_t children_{0};
    double arrival_mid_{0.0};
    double end_mid_{0.0};
    double post_mid_{0.0};
    Quantity market_volume_start_{0};
    double market_notional_start_{0.0};
    Quantity market_volume_end_{0};
    double market_notional_end_{0.0};
    std::vector<Quantity> fill_path_;
};

}  // namespace mml
