#pragma once

#include <cstdint>
#include <vector>

#include "mml/analytics/stats.hpp"
#include "mml/core/types.hpp"
#include "mml/sim/market_simulator.hpp"

namespace mml {

// Parametric impact model in the Almgren-Chriss form, all in ticks and steps:
//   temporary cost per unit when trading at rate v units/step: epsilon + eta * v
//   permanent price shift per unit traded:                     gamma
//   volatility of the mid price:                               sigma per sqrt(step)
struct ImpactParams {
    double sigma{0.0};
    double epsilon{0.0};
    double eta{0.0};
    double gamma{0.0};
};

[[nodiscard]] double temporary_impact(const ImpactParams& p, double rate) noexcept;
[[nodiscard]] double permanent_impact(const ImpactParams& p, double quantity) noexcept;

// Empirical square-root law: cost per unit = Y * sigma * sqrt(Q / V).
[[nodiscard]] double square_root_impact(double y, double sigma, double quantity, double volume) noexcept;

struct ImpactSample {
    Quantity size{0};
    double cost_per_unit{0.0};  // side * (avg fill - mid before), ticks
    double permanent{0.0};      // side * (mid after lag - mid before), ticks
};

struct ImpactCalibration {
    ImpactParams params{};
    OlsFit temporary_fit{};
    double sqrt_law_y{0.0};
    double avg_volume_per_step{0.0};
    std::vector<ImpactSample> samples;
};

struct CalibrationConfig {
    std::vector<Quantity> probe_sizes{2, 5, 10, 20, 30, 40};
    std::size_t seeds{5};
    std::size_t probe_interval{25};  // steps between probes
    std::size_t permanent_lag{10};   // steps after a probe to measure the permanent move
    std::size_t sigma_horizon{20};   // steps per return when estimating sigma (reduces bid-ask bounce)
};

// Measure impact in the simulator itself: a probe agent sends market orders of
// known sizes at regular intervals, and the resulting costs and price moves
// are regressed to recover (epsilon, eta, gamma). Sigma comes from multi-step
// mid returns. The fitted parameters feed the Almgren-Chriss scheduler.
[[nodiscard]] ImpactCalibration calibrate_impact(const SimConfig& base, const CalibrationConfig& cfg = {});

}  // namespace mml
