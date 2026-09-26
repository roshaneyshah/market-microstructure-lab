#include <cmath>
#include <memory>
#include <numeric>

#include "mml/exec/almgren_chriss.hpp"
#include "mml/exec/algorithms.hpp"
#include "mml/exec/execution_agent.hpp"
#include "test_framework.hpp"

using namespace mml;

namespace {
AlmgrenChrissParams base_params() {
    AlmgrenChrissParams p;
    p.quantity = 1000;
    p.intervals = 50;
    p.tau = 1.0;
    p.sigma = 0.4;
    p.epsilon = 0.8;
    p.eta = 0.05;
    p.gamma = 0.01;
    return p;
}
}  // namespace

TEST(ac_zero_risk_aversion_is_linear) {
    auto p = base_params();
    p.lambda = 0.0;
    const auto x = ac_holdings(p);
    CHECK_EQ(x.size(), 51u);
    for (std::size_t j = 0; j < x.size(); ++j) CHECK_NEAR(x[j], 1000.0 * (1.0 - j / 50.0), 1e-9);
}

TEST(ac_trajectory_properties) {
    auto p = base_params();
    p.lambda = 1e-3;
    const auto x = ac_holdings(p);
    const auto n = ac_trade_list(p);
    CHECK_NEAR(x.front(), 1000.0, 1e-9);
    CHECK_NEAR(x.back(), 0.0, 1e-12);
    CHECK_NEAR(std::accumulate(n.begin(), n.end(), 0.0), 1000.0, 1e-6);
    for (std::size_t j = 1; j < n.size(); ++j) CHECK(n[j] <= n[j - 1] + 1e-9);  // front loaded
    CHECK(ac_kappa(p) > 0.0);
    // Discrete kappa satisfies its defining equation.
    const double k = ac_kappa(p);
    const double lhs = 2.0 / (p.tau * p.tau) * (std::cosh(k * p.tau) - 1.0);
    CHECK_NEAR(lhs, p.lambda * p.sigma * p.sigma / ac_eta_tilde(p), 1e-12);
}

TEST(ac_efficient_frontier_is_monotone) {
    auto p = base_params();
    double prev_e = -1.0, prev_v = 1e300;
    for (double lambda : {0.0, 1e-4, 1e-3, 1e-2, 1e-1}) {
        p.lambda = lambda;
        const double e = ac_expected_cost(p, ac_trade_list(p));
        const double v = ac_variance(p, ac_holdings(p));
        CHECK(e >= prev_e - 1e-9);  // more urgency costs more in expectation
        CHECK(v <= prev_v + 1e-9);  // and reduces risk
        prev_e = e;
        prev_v = v;
    }
}

TEST(ac_optimal_minimises_mean_variance_objective) {
    auto p = base_params();
    p.lambda = 5e-3;
    const auto x_opt = ac_holdings(p);
    const double obj_opt = ac_expected_cost(p, ac_trade_list(p)) + p.lambda * ac_variance(p, x_opt);
    // Perturb the interior of the trajectory; the objective must not improve.
    for (std::size_t j = 1; j + 1 < x_opt.size(); j += 7) {
        for (double d : {-5.0, 5.0}) {
            auto x = x_opt;
            x[j] += d;
            std::vector<double> n(x.size() - 1);
            for (std::size_t i = 1; i < x.size(); ++i) n[i - 1] = x[i - 1] - x[i];
            const double obj = ac_expected_cost(p, n) + p.lambda * ac_variance(p, x);
            CHECK(obj >= obj_opt - 1e-6);
        }
    }
}

TEST(schedules_sum_to_target) {
    Twap twap;
    twap.on_start(1001, 37);
    CHECK_EQ(twap.schedule().back(), 1001);
    Vwap vwap(std::vector<double>(20, 1.0));
    vwap.on_start(500, 20);
    CHECK_EQ(vwap.schedule().back(), 500);
    CHECK_EQ(vwap.schedule()[9], 250);
    auto p = base_params();
    p.lambda = 1e-2;
    AlmgrenChrissAlgo ac(p);
    ac.on_start(300, 30);
    CHECK_EQ(ac.schedule().back(), 300);
    CHECK(ac.schedule()[4] > twap.schedule()[4] * 300 / 1001);
}

TEST(schedule_catches_up_after_shortfall) {
    Twap twap;
    twap.on_start(100, 10);
    ExecContext c;
    c.horizon = 10;
    c.target = 100;
    c.step = 3;
    c.executed = 10;  // behind schedule (should be 40 by end of step 3)
    CHECK_EQ(twap.child_quantity(c), 30);
    c.executed = 100;
    CHECK_EQ(twap.child_quantity(c), 0);
}

TEST(pov_tracks_participation) {
    Pov pov(0.2);
    ExecContext c;
    c.horizon = 100;
    c.target = 1000;
    Quantity total = 0;
    for (std::size_t s = 0; s < 50; ++s) {
        c.step = s;
        c.last_market_volume = 40;
        c.executed = total;
        total += pov.child_quantity(c);
    }
    // 0.2 / 0.8 * 40 = 10 per step.
    CHECK_EQ(total, 500);
}

TEST(execution_agent_end_to_end) {
    SimConfig cfg;
    cfg.steps = 260;
    cfg.seed = 3;
    MarketSimulator sim(cfg);
    ParentOrder parent{Side::Buy, 200, 10, 200};
    ExecutionAgent agent(parent, std::make_unique<Twap>());
    sim.add_agent(agent);
    sim.run();
    const auto r = agent.report();
    CHECK_EQ(r.filled, 200);
    CHECK_NEAR(r.fill_rate, 1.0, 1e-12);
    CHECK_EQ(agent.fill_path().size(), 200u);
    CHECK(r.arrival_mid > 0.0);
    CHECK(r.market_vwap > 0.0);
    // Buying with market orders pays at least part of the spread on average.
    CHECK(r.execution_cost > 0.0);
    CHECK(std::isfinite(r.shortfall_bps));
}
