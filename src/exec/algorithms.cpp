#include "mml/exec/algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace mml {

void ScheduleAlgorithm::on_start(Quantity target, std::size_t horizon) {
    const std::vector<double> frac = cumulative_fractions(horizon);
    cumulative_.assign(frac.size(), 0);
    for (std::size_t i = 0; i < frac.size(); ++i) {
        cumulative_[i] = static_cast<Quantity>(std::llround(std::clamp(frac[i], 0.0, 1.0) * static_cast<double>(target)));
    }
    if (!cumulative_.empty()) {
        cumulative_.back() = target;
    }
}

Quantity ScheduleAlgorithm::child_quantity(const ExecContext& ctx) {
    if (ctx.step >= cumulative_.size()) {
        return 0;
    }
    const Quantity want = cumulative_[ctx.step] - ctx.executed;
    return std::clamp<Quantity>(want, 0, ctx.target - ctx.executed);
}

std::vector<double> Twap::cumulative_fractions(std::size_t horizon) const {
    std::vector<double> f(horizon);
    for (std::size_t i = 0; i < horizon; ++i) {
        f[i] = static_cast<double>(i + 1) / static_cast<double>(horizon);
    }
    return f;
}

std::vector<double> Vwap::cumulative_fractions(std::size_t horizon) const {
    std::vector<double> w(horizon, 1.0);
    for (std::size_t i = 0; i < horizon && i < profile_.size(); ++i) {
        w[i] = std::max(0.0, profile_[i]);
    }
    const double total = std::accumulate(w.begin(), w.end(), 0.0);
    std::vector<double> f(horizon);
    double run = 0.0;
    for (std::size_t i = 0; i < horizon; ++i) {
        run += total > 0.0 ? w[i] / total : 1.0 / static_cast<double>(horizon);
        f[i] = run;
    }
    return f;
}

std::string AlmgrenChrissAlgo::name() const {
    if (!label_.empty()) {
        return label_;
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "AC(lambda=%.2g)", params_.lambda);
    return buf;
}

std::vector<double> AlmgrenChrissAlgo::cumulative_fractions(std::size_t horizon) const {
    AlmgrenChrissParams p = params_;
    p.quantity = 1.0;
    p.intervals = horizon;
    const std::vector<double> x = ac_holdings(p);
    std::vector<double> f(horizon);
    for (std::size_t i = 0; i < horizon; ++i) {
        f[i] = 1.0 - x[i + 1];
    }
    return f;
}

std::string Pov::name() const {
    char buf[48];
    std::snprintf(buf, sizeof(buf), complete_ ? "POV(%.0f%%,complete)" : "POV(%.0f%%)", rate_ * 100.0);
    return buf;
}

Quantity Pov::child_quantity(const ExecContext& ctx) {
    const Quantity remaining = ctx.target - ctx.executed;
    if (remaining <= 0) {
        return 0;
    }
    if (complete_ && ctx.step + 1 >= ctx.horizon) {
        return remaining;
    }
    // Participation r of total volume means own = r / (1 - r) * others.
    const double r = std::clamp(rate_, 0.0, 0.95);
    carry_ += r / (1.0 - r) * static_cast<double>(ctx.last_market_volume);
    const auto q = static_cast<Quantity>(std::floor(carry_));
    carry_ -= static_cast<double>(q);
    return std::min(q, remaining);
}

}  // namespace mml
