#pragma once

#include "mml/core/order_book.hpp"

namespace mml {

// Top-of-book microstructure signals, updated once per step.
//
// Queue imbalance:  I = (q_bid - q_ask) / (q_bid + q_ask), in [-1, 1].
// Order-flow imbalance (Cont, Kukanov and Stoikov 2014), per update:
//   e = 1{Pb >= Pb'} qb - 1{Pb <= Pb'} qb' - 1{Pa <= Pa'} qa + 1{Pa >= Pa'} qa'
// where primes denote the previous snapshot. Positive OFI means net buying
// pressure at the touch. The raw OFI is smoothed with an EWMA and normalised
// by average top-of-book depth so it is comparable across books.
// Mid-price volatility is tracked with an EWMA of squared mid changes.
class OrderFlowSignals {
public:
    explicit OrderFlowSignals(double ewma_alpha = 0.1, double vol_alpha = 0.02)
        : alpha_(ewma_alpha), vol_alpha_(vol_alpha) {}

    void update(const OrderBook& book, double mid);

    [[nodiscard]] double imbalance() const noexcept { return imbalance_; }
    [[nodiscard]] double ofi() const noexcept { return ofi_; }
    [[nodiscard]] double ofi_ewma() const noexcept { return ofi_ewma_; }
    [[nodiscard]] double ofi_normalized() const noexcept { return depth_ewma_ > 0.0 ? ofi_ewma_ / depth_ewma_ : 0.0; }
    // EWMA estimate of mid volatility, ticks per sqrt(step). 0 until warmed up.
    [[nodiscard]] double sigma() const noexcept;
    [[nodiscard]] bool ready() const noexcept { return updates_ > 10; }

private:
    double alpha_;
    double vol_alpha_;
    bool has_prev_{false};
    Price prev_bid_{0};
    Price prev_ask_{0};
    Quantity prev_bid_qty_{0};
    Quantity prev_ask_qty_{0};
    double prev_mid_{0.0};
    double imbalance_{0.0};
    double ofi_{0.0};
    double ofi_ewma_{0.0};
    double depth_ewma_{0.0};
    double var_ewma_{0.0};
    long updates_{0};
};

}  // namespace mml
