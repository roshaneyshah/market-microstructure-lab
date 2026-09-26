#include "mml/mm/signals.hpp"

#include <cmath>

namespace mml {

void OrderFlowSignals::update(const OrderBook& book, double mid) {
    const auto bid = book.best_bid();
    const auto ask = book.best_ask();
    if (!bid || !ask) {
        return;
    }
    const Quantity qb = book.top_quantity(Side::Buy);
    const Quantity qa = book.top_quantity(Side::Sell);
    const double total = static_cast<double>(qb + qa);
    imbalance_ = total > 0.0 ? static_cast<double>(qb - qa) / total : 0.0;

    if (has_prev_) {
        double e = 0.0;
        if (*bid >= prev_bid_) e += static_cast<double>(qb);
        if (*bid <= prev_bid_) e -= static_cast<double>(prev_bid_qty_);
        if (*ask <= prev_ask_) e -= static_cast<double>(qa);
        if (*ask >= prev_ask_) e += static_cast<double>(prev_ask_qty_);
        ofi_ = e;
        ofi_ewma_ = (1.0 - alpha_) * ofi_ewma_ + alpha_ * e;
        const double dm = mid - prev_mid_;
        var_ewma_ = updates_ == 0 ? dm * dm : (1.0 - vol_alpha_) * var_ewma_ + vol_alpha_ * dm * dm;
        ++updates_;
    }
    depth_ewma_ = depth_ewma_ == 0.0 ? 0.5 * total : (1.0 - alpha_) * depth_ewma_ + alpha_ * 0.5 * total;

    prev_bid_ = *bid;
    prev_ask_ = *ask;
    prev_bid_qty_ = qb;
    prev_ask_qty_ = qa;
    prev_mid_ = mid;
    has_prev_ = true;
}

double OrderFlowSignals::sigma() const noexcept { return std::sqrt(var_ewma_); }

}  // namespace mml
