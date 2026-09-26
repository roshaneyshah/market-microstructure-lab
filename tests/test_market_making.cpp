#include <cmath>
#include <cstdlib>

#include "mml/mm/market_maker.hpp"
#include "mml/mm/markout.hpp"
#include "test_framework.hpp"

using namespace mml;

TEST(avellaneda_stoikov_formulas) {
    AvellanedaStoikovParams p;
    p.gamma = 0.1;
    p.k = 1.5;
    p.horizon_steps = 100;
    AvellanedaStoikovMarketMaker mm(p);
    const double sigma = 0.4;
    CHECK_NEAR(mm.reservation_price(100.0, 0.0, sigma), 100.0, 1e-12);
    CHECK_NEAR(mm.reservation_price(100.0, 2.0, sigma), 100.0 - 2.0 * 0.1 * 0.16 * 100.0, 1e-12);
    CHECK(mm.reservation_price(100.0, -2.0, sigma) > 100.0);
    CHECK_NEAR(mm.optimal_spread(sigma), 0.1 * 0.16 * 100.0 + 20.0 * std::log(1.0 + 0.1 / 1.5), 1e-12);
}

TEST(signals_imbalance_and_ofi) {
    OrderBook b;
    std::vector<Trade> t;
    b.add(OrderRequest::limit(1, Side::Buy, 99, 30), t);
    b.add(OrderRequest::limit(2, Side::Sell, 101, 10), t);
    OrderFlowSignals s(1.0);
    s.update(b, 100.0);
    CHECK_NEAR(s.imbalance(), 0.5, 1e-12);
    b.add(OrderRequest::limit(3, Side::Buy, 99, 10), t);  // bid queue grows by 10
    s.update(b, 100.0);
    CHECK_NEAR(s.ofi(), 10.0, 1e-12);
    b.add(OrderRequest::market(4, Side::Buy, 4), t);      // ask queue shrinks by 4
    s.update(b, 100.0);
    CHECK_NEAR(s.ofi(), 4.0, 1e-12);
}

namespace {
template <class MM>
void run_mm(MM& mm, std::uint64_t seed, std::size_t steps = 3000) {
    SimConfig cfg;
    cfg.steps = steps;
    cfg.seed = seed;
    MarketSimulator sim(cfg);
    sim.add_agent(mm);
    sim.run();
    std::string why;
    CHECK(sim.engine().book().check_invariants(&why));
}
}  // namespace

TEST(market_makers_trade_and_attribution_reconciles) {
    const FeeSchedule fees{-0.1, 0.3};
    SymmetricMarketMaker sym(SymmetricParams{}, fees);
    InventoryAwareMarketMaker inv(InventoryParams{}, fees);
    AvellanedaStoikovMarketMaker as(AvellanedaStoikovParams{}, fees);
    run_mm(sym, 11);
    run_mm(inv, 11);
    run_mm(as, 11);
    for (const MarketMaker* mm : {static_cast<const MarketMaker*>(&sym), static_cast<const MarketMaker*>(&inv),
                                  static_cast<const MarketMaker*>(&as)}) {
        const auto& p = mm->portfolio();
        CHECK(p.stats().fills > 50);
        CHECK_EQ(p.stats().maker_fills, p.stats().fills);  // quotes are post-only
        CHECK_NEAR(p.attribution().total(), p.total_pnl(), 1e-6);
        CHECK(p.attribution().spread_capture > 0.0);
    }
}

TEST(inventory_limits_are_respected) {
    InventoryParams ip;
    ip.max_inventory = 20;
    InventoryAwareMarketMaker inv(ip);
    run_mm(inv, 21);
    CHECK(inv.portfolio().stats().max_abs_position <= 20);
    AvellanedaStoikovParams ap;
    ap.max_inventory = 20;
    AvellanedaStoikovMarketMaker as(ap);
    run_mm(as, 21);
    CHECK(as.portfolio().stats().max_abs_position <= 20);
}

TEST(markout_decomposition) {
    std::vector<StepRecord> h(5);
    for (std::size_t i = 0; i < h.size(); ++i) h[i].mid = 100.0 + static_cast<double>(i);
    std::vector<FillRecord> fills{{0, Side::Buy, 99, 2, 100.0, true}, {1, Side::Sell, 102, 1, 101.0, true}};
    const auto m = compute_markouts(fills, h, {0, 2});
    CHECK_EQ(m[0].fills, 2u);
    // spread capture: (1*2 + 1*1)/3 = 1
    CHECK_NEAR(m[0].spread_capture, 1.0, 1e-12);
    // adverse at h=0: buy (100-100)*2 + sell -(101-101)*1 = 0
    CHECK_NEAR(m[0].adverse_move, 0.0, 1e-12);
    // h=2: buy (102-100)*2 = 4, sell -(103-101)*1 = -2  -> 2/3
    CHECK_NEAR(m[1].adverse_move, 2.0 / 3.0, 1e-12);
    CHECK_NEAR(m[1].markout, m[1].spread_capture + m[1].adverse_move, 1e-12);
}
