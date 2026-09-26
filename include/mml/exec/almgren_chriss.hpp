#pragma once

#include <cstddef>
#include <vector>

namespace mml {

// Discrete-time Almgren-Chriss (2000) optimal liquidation of X units over N
// intervals of length tau, with linear temporary impact (epsilon + eta * v),
// linear permanent impact gamma, arithmetic Brownian price with volatility
// sigma, and risk aversion lambda (mean-variance: minimise E + lambda * V).
struct AlmgrenChrissParams {
    double quantity{0.0};  // X, units to trade (sign handled by the caller)
    std::size_t intervals{1};  // N
    double tau{1.0};
    double sigma{0.0};
    double epsilon{0.0};
    double eta{0.0};
    double gamma{0.0};
    double lambda{0.0};
};

// eta adjusted for the discrete-time permanent impact term: eta - gamma*tau/2.
[[nodiscard]] double ac_eta_tilde(const AlmgrenChrissParams& p) noexcept;

// Urgency parameter kappa, from (2/tau^2)(cosh(kappa tau) - 1) = lambda sigma^2 / eta_tilde.
[[nodiscard]] double ac_kappa(const AlmgrenChrissParams& p) noexcept;

// Holdings x_0 = X, ..., x_N = 0 (size N + 1). x_j = X sinh(kappa (T - t_j)) / sinh(kappa T).
// lambda = 0 degenerates to the linear (TWAP) schedule.
[[nodiscard]] std::vector<double> ac_holdings(const AlmgrenChrissParams& p);

// Trade sizes n_j = x_{j-1} - x_j for j = 1..N (size N).
[[nodiscard]] std::vector<double> ac_trade_list(const AlmgrenChrissParams& p);

// Expected implementation shortfall of a trade list, in price units * quantity:
//   E = gamma X^2 / 2 + epsilon sum |n_j| + (eta_tilde / tau) sum n_j^2
[[nodiscard]] double ac_expected_cost(const AlmgrenChrissParams& p, const std::vector<double>& trades);

// Variance of implementation shortfall: V = sigma^2 tau sum_{j=1..N} x_j^2
[[nodiscard]] double ac_variance(const AlmgrenChrissParams& p, const std::vector<double>& holdings);

// Half-life of the optimal trajectory in time units (ln 2 / kappa); infinite for kappa = 0.
[[nodiscard]] double ac_half_life(const AlmgrenChrissParams& p) noexcept;

}  // namespace mml
