#include <vector>

#include "mml/engine/matching_engine.hpp"
#include "mml/engine/order_gateway.hpp"
#include "test_framework.hpp"

using namespace mml;

namespace {
struct Recorder : FillListener {
    std::vector<Fill> fills;
    void on_fill(const Fill& f) override { fills.push_back(f); }
};
}  // namespace

TEST(engine_stamps_monotonic_timestamps_and_stats) {
    MatchingEngine e;
    e.keep_tape(true);
    e.submit(OrderRequest::limit(1, Side::Sell, 100, 5));
    e.submit(OrderRequest::limit(2, Side::Sell, 101, 5));
    const auto r = e.submit(OrderRequest::market(3, Side::Buy, 7));
    CHECK_EQ(r.filled, 7);
    CHECK_EQ(e.last_trades().size(), 2u);
    CHECK(e.last_trades()[0].ts < e.clock() + 1);
    CHECK_EQ(e.last_trades()[0].ts, e.last_trades()[1].ts);
    CHECK_EQ(e.stats().volume, 7);
    CHECK_EQ(e.stats().signed_volume, 7);
    CHECK_NEAR(e.stats().notional, 100.0 * 5 + 101.0 * 2, 1e-9);
    CHECK_EQ(e.stats().trades, 2u);
    CHECK_EQ(e.tape().size(), 2u);
    CHECK(e.cancel(2));
    CHECK_EQ(e.stats().cancels, 1u);
    CHECK(e.last_trades().empty());
}

TEST(gateway_assigns_ids_and_routes_fills_to_both_sides) {
    MatchingEngine e;
    OrderGateway g(e);
    Recorder maker, taker;
    const TraderId m = g.register_trader(&maker);
    const TraderId t = g.register_trader(&taker);
    CHECK(m != t);
    const auto a = g.limit(m, Side::Sell, 100, 5);
    const auto b = g.limit(m, Side::Buy, 98, 5);
    CHECK(a.id != b.id);
    CHECK_NEAR(g.reference_mid(), 99.0, 1e-12);
    g.market(t, Side::Buy, 3);
    CHECK_EQ(maker.fills.size(), 1u);
    CHECK_EQ(taker.fills.size(), 1u);
    CHECK(maker.fills[0].maker);
    CHECK(maker.fills[0].side == Side::Sell);
    CHECK(!taker.fills[0].maker);
    CHECK(taker.fills[0].side == Side::Buy);
    CHECK_EQ(maker.fills[0].order, a.id);
    CHECK_NEAR(maker.fills[0].reference_mid, 99.0, 1e-12);
}

TEST(gateway_enforces_ownership_and_risk_limits) {
    MatchingEngine e;
    RiskLimits lim;
    lim.max_order_quantity = 10;
    OrderGateway g(e, lim);
    const TraderId a = g.register_trader();
    const TraderId b = g.register_trader();
    const auto r = g.limit(a, Side::Buy, 100, 5);
    CHECK(!g.cancel(b, r.id));
    CHECK(g.modify(b, r.id, 101, 5).result.reason == RejectReason::NotOwner);
    CHECK(g.limit(a, Side::Buy, 100, 11).result.reason == RejectReason::RiskLimit);
    CHECK(g.modify(a, r.id, 100, 11).result.reason == RejectReason::RiskLimit);
    CHECK(g.cancel(a, r.id));
    CHECK(g.modify(a, r.id, 100, 5).result.reason == RejectReason::UnknownOrder);
}

TEST(gateway_reference_mid_survives_one_sided_book) {
    MatchingEngine e;
    OrderGateway g(e);
    const TraderId a = g.register_trader();
    g.limit(a, Side::Buy, 99, 1);
    g.limit(a, Side::Sell, 101, 1);
    CHECK_NEAR(g.reference_mid(), 100.0, 1e-12);
    g.market(a, Side::Buy, 1);  // empties the ask side
    CHECK_NEAR(g.reference_mid(), 100.0, 1e-12);
}
