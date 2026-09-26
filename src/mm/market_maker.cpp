#include "mml/mm/market_maker.hpp"

#include <algorithm>
#include <cmath>

namespace mml {

MarketMaker::MarketMaker(std::string name, FeeSchedule fees) : name_(std::move(name)), portfolio_(fees) {}

Price MarketMaker::bid_price(double x) noexcept { return static_cast<Price>(std::floor(x + 1e-9)); }
Price MarketMaker::ask_price(double x) noexcept { return static_cast<Price>(std::ceil(x - 1e-9)); }

void MarketMaker::on_step(SimContext& ctx) {
    step_ = ctx.step;
    portfolio_.mark(ctx.mid);
    pnl_path_.push_back(portfolio_.total_pnl());
    inventory_path_.push_back(portfolio_.position());
    signals_.update(ctx.book, ctx.mid);

    QuotePair q = compute_quotes(ctx);

    // Post-only: never cross the current book.
    if (q.bid) {
        if (const auto a = ctx.book.best_ask()) {
            q.bid->price = std::min(q.bid->price, *a - 1);
        }
        if (q.bid->price <= 0 || q.bid->quantity <= 0) q.bid.reset();
    }
    reconcile(ctx, Side::Buy, q.bid, bid_);
    if (q.ask) {
        if (const auto b = ctx.book.best_bid()) {
            q.ask->price = std::max(q.ask->price, *b + 1);
        }
        if (q.ask->quantity <= 0) q.ask.reset();
    }
    reconcile(ctx, Side::Sell, q.ask, ask_);
}

void MarketMaker::reconcile(SimContext& ctx, Side side, std::optional<Quote> want, LiveOrder& live) {
    if (live.id != kNoOrder && want && live.price == want->price && live.remaining > 0) {
        return;  // keep queue priority
    }
    if (live.id != kNoOrder) {
        ctx.gateway.cancel(ctx.self, live.id);
        live = LiveOrder{};
    }
    if (!want) {
        return;
    }
    const SubmitResult r = ctx.gateway.limit(ctx.self, side, want->price, want->quantity);
    ++quote_updates_;
    if (r.result.resting > 0) {
        live = LiveOrder{r.id, want->price, r.result.resting};
    }
}

void MarketMaker::on_fill(const Fill& fill) {
    portfolio_.on_fill(fill.side, fill.price, fill.quantity, fill.maker, fill.reference_mid);
    fills_.push_back(FillRecord{step_, fill.side, fill.price, fill.quantity, fill.reference_mid, fill.maker});
    for (LiveOrder* live : {&bid_, &ask_}) {
        if (live->id == fill.order) {
            live->remaining -= fill.quantity;
            if (live->remaining <= 0) {
                *live = LiveOrder{};
            }
        }
    }
}

void MarketMaker::on_finish(SimContext& ctx) {
    for (LiveOrder* live : {&bid_, &ask_}) {
        if (live->id != kNoOrder) {
            ctx.gateway.cancel(ctx.self, live->id);
            *live = LiveOrder{};
        }
    }
    portfolio_.mark(ctx.mid);
    pnl_path_.push_back(portfolio_.total_pnl());
    inventory_path_.push_back(portfolio_.position());
}

// ---- Symmetric --------------------------------------------------------------

SymmetricMarketMaker::SymmetricMarketMaker(SymmetricParams p, FeeSchedule fees, std::string name)
    : MarketMaker(std::move(name), fees), p_(p) {}

QuotePair SymmetricMarketMaker::compute_quotes(const SimContext& ctx) {
    QuotePair q;
    q.bid = Quote{bid_price(ctx.mid - p_.half_spread), p_.size};
    q.ask = Quote{ask_price(ctx.mid + p_.half_spread), p_.size};
    return q;
}

// ---- Inventory aware -------------------------------------------------------

InventoryAwareMarketMaker::InventoryAwareMarketMaker(InventoryParams p, FeeSchedule fees, std::string name)
    : MarketMaker(std::move(name), fees), p_(p) {}

QuotePair InventoryAwareMarketMaker::compute_quotes(const SimContext& ctx) {
    const Quantity inv = portfolio().position();
    const double center = ctx.mid - p_.skew_per_unit * static_cast<double>(inv);
    QuotePair q;
    if (inv + p_.size <= p_.max_inventory) {
        q.bid = Quote{bid_price(center - p_.half_spread), p_.size};
    }
    if (inv - p_.size >= -p_.max_inventory) {
        q.ask = Quote{ask_price(center + p_.half_spread), p_.size};
    }
    return q;
}

// ---- Avellaneda-Stoikov ----------------------------------------------------

AvellanedaStoikovMarketMaker::AvellanedaStoikovMarketMaker(AvellanedaStoikovParams p, FeeSchedule fees,
                                                           std::string name)
    : MarketMaker(std::move(name), fees), p_(p) {}

double AvellanedaStoikovMarketMaker::reservation_price(double mid, double inventory_lots,
                                                       double sigma) const noexcept {
    return mid - inventory_lots * p_.gamma * sigma * sigma * p_.horizon_steps;
}

double AvellanedaStoikovMarketMaker::optimal_spread(double sigma) const noexcept {
    return p_.gamma * sigma * sigma * p_.horizon_steps + (2.0 / p_.gamma) * std::log1p(p_.gamma / p_.k);
}

QuotePair AvellanedaStoikovMarketMaker::compute_quotes(const SimContext& ctx) {
    double sigma = p_.sigma;
    if (sigma <= 0.0) {
        // Online estimate, floored so a quiet warm-up does not collapse the spread.
        sigma = signals().ready() ? std::max(signals().sigma(), 0.1) : 0.4;
    }
    const Quantity inv = portfolio().position();
    const double lots = static_cast<double>(inv) / static_cast<double>(std::max<Quantity>(p_.size, 1));
    double r = reservation_price(ctx.mid, lots, sigma);
    r += p_.alpha_imbalance * signals().imbalance() + p_.alpha_ofi * signals().ofi_normalized();
    const double half = std::max(0.5 * optimal_spread(sigma), p_.min_half_spread);

    QuotePair q;
    if (inv + p_.size <= p_.max_inventory) {
        q.bid = Quote{bid_price(r - half), p_.size};
    }
    if (inv - p_.size >= -p_.max_inventory) {
        q.ask = Quote{ask_price(r + half), p_.size};
    }
    return q;
}

}  // namespace mml
