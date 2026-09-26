#include "mml/exec/execution_agent.hpp"

#include <algorithm>

namespace mml {

ExecutionAgent::ExecutionAgent(ParentOrder parent, std::unique_ptr<ExecutionAlgorithm> algo)
    : parent_(parent), algo_(std::move(algo)) {
    fill_path_.reserve(parent_.horizon);
}

void ExecutionAgent::on_step(SimContext& ctx) {
    const std::size_t end = parent_.start_step + parent_.horizon;
    const MarketStats& ms = ctx.gateway.engine().stats();

    if (!started_ && ctx.step >= parent_.start_step) {
        started_ = true;
        arrival_mid_ = ctx.mid;
        market_volume_start_ = ms.volume;
        market_notional_start_ = ms.notional;
        algo_->on_start(parent_.quantity, parent_.horizon);
    }
    if (started_ && !ended_ && ctx.step > parent_.start_step) {
        fill_path_.push_back(filled_);
    }
    if (started_ && !ended_ && ctx.step >= end) {
        ended_ = true;
        end_mid_ = ctx.mid;
        market_volume_end_ = ms.volume;
        market_notional_end_ = ms.notional;
        return;
    }
    if (!started_ || ended_) {
        return;
    }

    ExecContext ec;
    ec.step = ctx.step - parent_.start_step;
    ec.horizon = parent_.horizon;
    ec.target = parent_.quantity;
    ec.executed = filled_;
    ec.last_market_volume = std::max<Quantity>(0, ctx.last_step_volume - own_volume_step_);
    ec.mid = ctx.mid;
    own_volume_step_ = 0;

    const Quantity q = algo_->child_quantity(ec);
    if (q > 0) {
        ctx.gateway.market(ctx.self, parent_.side, q);
        ++children_;
    }
}

void ExecutionAgent::on_finish(SimContext& ctx) {
    const MarketStats& ms = ctx.gateway.engine().stats();
    if (started_ && !ended_) {
        ended_ = true;
        end_mid_ = ctx.mid;
        market_volume_end_ = ms.volume;
        market_notional_end_ = ms.notional;
        fill_path_.push_back(filled_);
    }
    post_mid_ = ctx.mid;
}

void ExecutionAgent::on_fill(const Fill& fill) {
    filled_ += fill.quantity;
    const double n = static_cast<double>(fill.price) * static_cast<double>(fill.quantity);
    notional_ += n;
    own_volume_step_ += fill.quantity;
    own_volume_total_ += fill.quantity;
    own_notional_total_ += n;
}

ExecutionReport ExecutionAgent::report() const {
    ExecutionReport r;
    r.algorithm = algo_->name();
    r.side = parent_.side;
    r.target = parent_.quantity;
    r.filled = filled_;
    r.child_orders = children_;
    r.arrival_mid = arrival_mid_;
    r.end_mid = end_mid_;
    r.post_mid = post_mid_;
    r.average_price = filled_ > 0 ? notional_ / static_cast<double>(filled_) : 0.0;

    const double s = sign(parent_.side);
    const double x = static_cast<double>(parent_.quantity);
    const double f = static_cast<double>(filled_);
    r.fill_rate = x > 0.0 ? f / x : 0.0;
    r.execution_cost = filled_ > 0 ? s * (r.average_price - arrival_mid_) * f : 0.0;
    const double opportunity = s * (end_mid_ - arrival_mid_) * (x - f);
    r.shortfall_ticks = x > 0.0 ? (r.execution_cost + opportunity) / x : 0.0;
    r.shortfall_bps = arrival_mid_ > 0.0 ? r.shortfall_ticks / arrival_mid_ * 1e4 : 0.0;
    r.market_impact = s * (end_mid_ - arrival_mid_);
    r.permanent_impact = s * (post_mid_ - arrival_mid_);

    // Horizon trades are exactly the trades between the start and end
    // snapshots, and every own fill happens inside the horizon.
    const auto others_volume = static_cast<double>(market_volume_end_ - market_volume_start_ - own_volume_total_);
    const double others_notional = market_notional_end_ - market_notional_start_ - own_notional_total_;
    r.market_vwap = others_volume > 0.0 ? others_notional / others_volume : arrival_mid_;
    r.slippage_vs_vwap_bps =
        (filled_ > 0 && r.market_vwap > 0.0) ? s * (r.average_price - r.market_vwap) / r.market_vwap * 1e4 : 0.0;
    return r;
}

}  // namespace mml
