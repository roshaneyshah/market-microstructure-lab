#include <cmath>
#include <numeric>

#include "mml/analytics/stats.hpp"
#include "mml/sim/impact_model.hpp"
#include "mml/sim/market_simulator.hpp"
#include "mml/sim/random.hpp"
#include "test_framework.hpp"

using namespace mml;

TEST(rng_is_deterministic_and_moments_are_sane) {
    Rng a(42), b(42);
    for (int i = 0; i < 100; ++i) CHECK_EQ(a.next(), b.next());
    Rng r(7);
    std::vector<double> n, pois, u;
    for (int i = 0; i < 200000; ++i) {
        n.push_back(r.normal());
        pois.push_back(static_cast<double>(r.poisson(3.5)));
        u.push_back(static_cast<double>(r.uniform_int(1, 6)));
    }
    CHECK_NEAR(mean(n), 0.0, 0.01);
    CHECK_NEAR(stddev(n), 1.0, 0.01);
    CHECK_NEAR(mean(pois), 3.5, 0.02);
    CHECK_NEAR(stddev(pois) * stddev(pois), 3.5, 0.05);
    CHECK_NEAR(mean(u), 3.5, 0.02);
    const auto s = summarize(u);
    CHECK_EQ(s.min, 1.0);
    CHECK_EQ(s.max, 6.0);
}

TEST(stats_summary_and_ols) {
    const auto s = summarize({5, 1, 4, 2, 3});
    CHECK_NEAR(s.mean, 3.0, 1e-12);
    CHECK_NEAR(s.median, 3.0, 1e-12);
    CHECK_NEAR(s.cvar95, 5.0, 1e-12);
    const auto f = ols({1, 2, 3, 4}, {3, 5, 7, 9});
    CHECK_NEAR(f.slope, 2.0, 1e-12);
    CHECK_NEAR(f.intercept, 1.0, 1e-12);
    CHECK_NEAR(f.r2, 1.0, 1e-12);
}

TEST(simulator_is_deterministic_for_a_seed) {
    SimConfig cfg;
    cfg.steps = 500;
    cfg.seed = 99;
    MarketSimulator a(cfg), b(cfg);
    a.run();
    b.run();
    CHECK_EQ(a.history().size(), 500u);
    for (std::size_t i = 0; i < a.history().size(); ++i) {
        CHECK_EQ(a.history()[i].mid, b.history()[i].mid);
        CHECK_EQ(a.history()[i].volume, b.history()[i].volume);
    }
    CHECK_EQ(a.engine().stats().trades, b.engine().stats().trades);
}

TEST(simulator_produces_a_reasonable_market) {
    SimConfig cfg;
    cfg.steps = 3000;
    cfg.seed = 5;
    MarketSimulator sim(cfg);
    sim.run();
    std::string why;
    CHECK(sim.engine().book().check_invariants(&why));
    std::vector<double> spread, vol;
    for (const auto& r : sim.history()) {
        if (r.best_bid > 0 && r.best_ask > 0) spread.push_back(static_cast<double>(r.best_ask - r.best_bid));
        vol.push_back(static_cast<double>(r.volume));
    }
    CHECK(spread.size() > 2900);        // book is almost always two-sided
    CHECK(mean(spread) >= 1.0);
    CHECK(mean(spread) < 4.0);
    CHECK(mean(vol) > 2.0);
    // Price tracks the fundamental.
    const auto& last = sim.history().back();
    CHECK(std::fabs(last.mid - last.fundamental) < 10.0);
}

TEST(seasonality_profile_has_unit_mean) {
    double sum = 0.0;
    const int n = 10000;
    for (int i = 0; i < n; ++i) sum += MarketSimulator::seasonality((i + 0.5) / n, 1.5);
    CHECK_NEAR(sum / n, 1.0, 1e-3);
    CHECK(MarketSimulator::seasonality(0.0, 1.5) > MarketSimulator::seasonality(0.5, 1.5));
    SimConfig cfg;
    cfg.steps = 100;
    cfg.flow.seasonality = 1.5;
    MarketSimulator sim(cfg);
    const auto w = sim.expected_volume_profile(0, 100);
    CHECK_NEAR(std::accumulate(w.begin(), w.end(), 0.0), 1.0, 1e-12);
}

TEST(impact_calibration_recovers_positive_impact) {
    SimConfig cfg;
    cfg.steps = 2500;
    CalibrationConfig cc;
    cc.seeds = 3;
    const auto cal = calibrate_impact(cfg, cc);
    CHECK(cal.samples.size() > 200);
    CHECK(cal.params.sigma > 0.1);
    CHECK(cal.params.eta > 0.0);
    CHECK(cal.params.epsilon > 0.2);  // at least part of the half spread
    CHECK(cal.params.gamma > 0.0);
    CHECK(cal.temporary_fit.slope > 0.0);
}
