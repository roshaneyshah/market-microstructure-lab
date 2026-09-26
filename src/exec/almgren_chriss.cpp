#include "mml/exec/almgren_chriss.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mml {

double ac_eta_tilde(const AlmgrenChrissParams& p) noexcept {
    // Must stay positive for the problem to be convex.
    return std::max(p.eta - 0.5 * p.gamma * p.tau, 1e-12);
}

double ac_kappa(const AlmgrenChrissParams& p) noexcept {
    if (p.lambda <= 0.0 || p.sigma <= 0.0) {
        return 0.0;
    }
    const double kappa_tilde_sq = p.lambda * p.sigma * p.sigma / ac_eta_tilde(p);
    return std::acosh(1.0 + 0.5 * kappa_tilde_sq * p.tau * p.tau) / p.tau;
}

std::vector<double> ac_holdings(const AlmgrenChrissParams& p) {
    const std::size_t n = std::max<std::size_t>(p.intervals, 1);
    std::vector<double> x(n + 1);
    const double big_t = static_cast<double>(n) * p.tau;
    const double kappa = ac_kappa(p);
    for (std::size_t j = 0; j <= n; ++j) {
        const double t = static_cast<double>(j) * p.tau;
        if (kappa * big_t < 1e-8) {
            x[j] = p.quantity * (1.0 - t / big_t);
        } else if (kappa * big_t > 700.0) {
            // Avoid overflow: sinh(a)/sinh(b) ~ exp(a - b) for large arguments.
            x[j] = p.quantity * std::exp(-kappa * t);
        } else {
            x[j] = p.quantity * std::sinh(kappa * (big_t - t)) / std::sinh(kappa * big_t);
        }
    }
    x[n] = 0.0;
    return x;
}

std::vector<double> ac_trade_list(const AlmgrenChrissParams& p) {
    const std::vector<double> x = ac_holdings(p);
    std::vector<double> trades(x.size() - 1);
    for (std::size_t j = 1; j < x.size(); ++j) {
        trades[j - 1] = x[j - 1] - x[j];
    }
    return trades;
}

double ac_expected_cost(const AlmgrenChrissParams& p, const std::vector<double>& trades) {
    double abs_sum = 0.0;
    double sq_sum = 0.0;
    for (double n : trades) {
        abs_sum += std::fabs(n);
        sq_sum += n * n;
    }
    return 0.5 * p.gamma * p.quantity * p.quantity + p.epsilon * abs_sum + ac_eta_tilde(p) / p.tau * sq_sum;
}

double ac_variance(const AlmgrenChrissParams& p, const std::vector<double>& holdings) {
    double sq = 0.0;
    for (std::size_t j = 1; j < holdings.size(); ++j) {
        sq += holdings[j] * holdings[j];
    }
    return p.sigma * p.sigma * p.tau * sq;
}

double ac_half_life(const AlmgrenChrissParams& p) noexcept {
    const double k = ac_kappa(p);
    return k > 0.0 ? std::log(2.0) / k : std::numeric_limits<double>::infinity();
}

}  // namespace mml
