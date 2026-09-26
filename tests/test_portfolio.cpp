#include "mml/portfolio/portfolio.hpp"
#include "mml/sim/random.hpp"
#include "test_framework.hpp"

using namespace mml;

TEST(portfolio_basic_round_trip) {
    Portfolio p;
    p.on_fill(Side::Buy, 100, 10, true, 100.5);
    CHECK_EQ(p.position(), 10);
    CHECK_NEAR(p.cash(), -1000.0, 1e-9);
    CHECK_NEAR(p.attribution().spread_capture, 5.0, 1e-9);  // bought 0.5 below mid
    p.on_fill(Side::Sell, 102, 10, true, 101.5);
    CHECK_EQ(p.position(), 0);
    CHECK_NEAR(p.total_pnl(), 20.0, 1e-9);
    CHECK_NEAR(p.realized_pnl(), 20.0, 1e-9);
    CHECK_NEAR(p.attribution().spread_capture, 10.0, 1e-9);
    CHECK_NEAR(p.attribution().inventory, 10.0, 1e-9);  // held 10 while mid rose 1
}

TEST(portfolio_fees_and_rebates) {
    Portfolio p(FeeSchedule{-0.2, 0.3});
    p.on_fill(Side::Buy, 100, 10, true, 100.0);   // rebate 2
    p.on_fill(Side::Sell, 100, 10, false, 100.0); // fee 3
    CHECK_NEAR(p.attribution().fees, 1.0, 1e-9);
    CHECK_NEAR(p.total_pnl(), -1.0, 1e-9);
}

TEST(portfolio_position_flip_cost_basis) {
    Portfolio p;
    p.on_fill(Side::Buy, 100, 5, true, 100);
    p.on_fill(Side::Sell, 104, 8, true, 104);
    CHECK_EQ(p.position(), -3);
    CHECK_NEAR(p.realized_pnl(), 20.0, 1e-9);
    CHECK_NEAR(p.average_cost(), 104.0, 1e-9);
}

// The attribution must reconcile exactly with mark-to-mid PnL, and realized +
// unrealized - fees must equal it too, for any sequence of fills and marks.
TEST(portfolio_attribution_identity_random) {
    Rng rng(123);
    for (int trial = 0; trial < 50; ++trial) {
        Portfolio p(FeeSchedule{-0.1, 0.25});
        double mid = 1000.0;
        for (int i = 0; i < 400; ++i) {
            mid += rng.normal();
            if (rng.bernoulli(0.5)) {
                p.mark(mid);
            } else {
                const Side s = rng.bernoulli(0.5) ? Side::Buy : Side::Sell;
                const Price px = static_cast<Price>(std::llround(mid)) + rng.uniform_int(-2, 2);
                p.on_fill(s, px, rng.uniform_int(1, 9), rng.bernoulli(0.7), mid);
            }
        }
        CHECK_NEAR(p.attribution().total(), p.total_pnl(), 1e-6);
        CHECK_NEAR(p.realized_pnl() + p.unrealized_pnl() - p.attribution().fees, p.total_pnl(), 1e-6);
    }
}
