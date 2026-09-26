#include "mml/sim/impact_model.hpp"

#include <algorithm>
#include <cmath>

#include "mml/sim/random.hpp"

namespace mml {

double temporary_impact(const ImpactParams& p, double rate) noexcept {
    if (rate == 0.0) {
        return 0.0;
    }
    const double s = rate > 0.0 ? 1.0 : -1.0;
    return s * p.epsilon + p.eta * rate;
}

double permanent_impact(const ImpactParams& p, double quantity) noexcept { return p.gamma * quantity; }

double square_root_impact(double y, double sigma, double quantity, double volume) noexcept {
    if (volume <= 0.0 || quantity <= 0.0) {
        return 0.0;
    }
    return y * sigma * std::sqrt(quantity / volume);
}

namespace {

class ProbeAgent final : public Agent {
public:
    ProbeAgent(const CalibrationConfig& cfg, std::uint64_t seed) : cfg_(cfg), rng_(seed) {}

    void on_step(SimContext& ctx) override {
        if (pending_ && ctx.step == probe_step_ + cfg_.permanent_lag) {
            const double s = sign(side_);
            if (filled_ > 0) {
                const double avg = notional_ / static_cast<double>(filled_);
                samples_.push_back(ImpactSample{filled_, s * (avg - mid_before_), s * (ctx.mid - mid_before_)});
            }
            pending_ = false;
        }
        if (!pending_ && ctx.step % cfg_.probe_interval == 0 && ctx.step + cfg_.permanent_lag < ctx.steps) {
            side_ = rng_.bernoulli(0.5) ? Side::Buy : Side::Sell;
            const Quantity size = cfg_.probe_sizes[next_size_++ % cfg_.probe_sizes.size()];
            mid_before_ = ctx.mid;
            probe_step_ = ctx.step;
            filled_ = 0;
            notional_ = 0.0;
            pending_ = true;
            ctx.gateway.market(ctx.self, side_, size);
        }
    }

    void on_fill(const Fill& f) override {
        filled_ += f.quantity;
        notional_ += static_cast<double>(f.price) * static_cast<double>(f.quantity);
    }

    [[nodiscard]] const std::vector<ImpactSample>& samples() const noexcept { return samples_; }

private:
    CalibrationConfig cfg_;
    Rng rng_;
    std::vector<ImpactSample> samples_;
    std::size_t next_size_{0};
    bool pending_{false};
    std::size_t probe_step_{0};
    Side side_{Side::Buy};
    double mid_before_{0.0};
    Quantity filled_{0};
    double notional_{0.0};
};

}  // namespace

ImpactCalibration calibrate_impact(const SimConfig& base, const CalibrationConfig& cfg) {
    ImpactCalibration out;
    std::vector<double> k_step_returns;
    double volume_sum = 0.0;
    std::size_t volume_n = 0;

    for (std::size_t s = 0; s < cfg.seeds; ++s) {
        SimConfig sc = base;
        sc.seed = base.seed + 7919 * (s + 1);
        MarketSimulator sim(sc);
        ProbeAgent probe(cfg, sc.seed ^ 0xA5A5A5A5ULL);
        sim.add_agent(probe);
        sim.run();
        out.samples.insert(out.samples.end(), probe.samples().begin(), probe.samples().end());

        const auto& h = sim.history();
        for (std::size_t i = cfg.sigma_horizon; i < h.size(); i += cfg.sigma_horizon) {
            k_step_returns.push_back(h[i].mid - h[i - cfg.sigma_horizon].mid);
        }
        for (const StepRecord& r : h) {
            volume_sum += static_cast<double>(r.volume);
            ++volume_n;
        }
    }

    std::vector<double> q, cost, perm, sqrt_x;
    out.avg_volume_per_step = volume_n > 0 ? volume_sum / static_cast<double>(volume_n) : 0.0;
    out.params.sigma = stddev(k_step_returns) / std::sqrt(static_cast<double>(cfg.sigma_horizon));
    for (const ImpactSample& smp : out.samples) {
        q.push_back(static_cast<double>(smp.size));
        cost.push_back(smp.cost_per_unit);
        perm.push_back(smp.permanent);
        sqrt_x.push_back(out.params.sigma * std::sqrt(static_cast<double>(smp.size) / out.avg_volume_per_step));
    }
    out.temporary_fit = ols(q, cost);
    out.params.epsilon = std::max(0.0, out.temporary_fit.intercept);
    out.params.eta = std::max(1e-6, out.temporary_fit.slope);
    out.params.gamma = std::max(0.0, slope_through_origin(q, perm));
    out.sqrt_law_y = slope_through_origin(sqrt_x, cost);
    return out;
}

}  // namespace mml
