#include <string>
#include <vector>

#include "mml/core/order_book.hpp"
#include "test_framework.hpp"

using namespace mml;

// Every scenario runs against all four book variants.
#define BOOK_TEST(name)                                          \
    template <class Book>                                        \
    static void name##_impl();                                   \
    TEST(name##_map_hash) { name##_impl<OrderBook>(); }           \
    TEST(name##_map_dense) { name##_impl<MapDenseOrderBook>(); }  \
    TEST(name##_ladder_hash) { name##_impl<LadderHashOrderBook>(); } \
    TEST(name##_ladder_dense) { name##_impl<LadderOrderBook>(); } \
    template <class Book>                                        \
    static void name##_impl()

namespace {

template <class Book>
ExecResult lim(Book& b, std::vector<Trade>& t, OrderId id, Side s, Price p, Quantity q,
               TimeInForce tif = TimeInForce::GTC) {
    return b.add(OrderRequest::limit(id, s, p, q, tif), t);
}

template <class Book>
void check_ok(const Book& b) {
    std::string why;
    const bool ok = b.check_invariants(&why);
    if (!ok) {
        throw test::Failure{"invariant violated: " + why};
    }
}

}  // namespace

BOOK_TEST(empty_book) {
    Book b;
    CHECK(!b.best_bid());
    CHECK(!b.best_ask());
    CHECK(!b.mid());
    CHECK(b.empty());
    check_ok(b);
}

BOOK_TEST(limit_orders_rest_and_set_top) {
    Book b;
    std::vector<Trade> t;
    CHECK(lim(b, t, 1, Side::Buy, 99, 10).status == OrderStatus::Accepted);
    CHECK(lim(b, t, 2, Side::Buy, 100, 5).status == OrderStatus::Accepted);
    CHECK(lim(b, t, 3, Side::Sell, 102, 7).status == OrderStatus::Accepted);
    CHECK(lim(b, t, 4, Side::Sell, 101, 3).status == OrderStatus::Accepted);
    CHECK(t.empty());
    CHECK_EQ(*b.best_bid(), 100);
    CHECK_EQ(*b.best_ask(), 101);
    CHECK_EQ(*b.spread(), 1);
    CHECK_NEAR(*b.mid(), 100.5, 1e-12);
    CHECK_EQ(b.order_count(), 4u);
    CHECK_EQ(b.level_count(Side::Buy), 2u);
    const auto bids = b.depth(Side::Buy, 10);
    CHECK_EQ(bids.size(), 2u);
    CHECK_EQ(bids[0].price, 100);
    CHECK_EQ(bids[1].price, 99);
    check_ok(b);
}

BOOK_TEST(price_time_priority) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 101, 5);
    lim(b, t, 2, Side::Sell, 100, 5);  // better price, later time
    lim(b, t, 3, Side::Sell, 100, 5);  // same price, even later
    const auto r = lim(b, t, 4, Side::Buy, 101, 12);
    CHECK(r.status == OrderStatus::Filled);
    CHECK_EQ(t.size(), 3u);
    CHECK_EQ(t[0].maker_order, 2u);  // best price first
    CHECK_EQ(t[0].price, 100);
    CHECK_EQ(t[1].maker_order, 3u);  // then time priority within the price
    CHECK_EQ(t[1].price, 100);
    CHECK_EQ(t[2].maker_order, 1u);
    CHECK_EQ(t[2].price, 101);
    CHECK_EQ(t[2].quantity, 2);
    CHECK_EQ(b.volume_at(Side::Sell, 101), 3);
    check_ok(b);
}

BOOK_TEST(trades_print_at_resting_price) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Buy, 100, 5);
    lim(b, t, 2, Side::Sell, 95, 5);
    CHECK_EQ(t.size(), 1u);
    CHECK_EQ(t[0].price, 100);
    CHECK(t[0].aggressor == Side::Sell);
    CHECK_EQ(t[0].taker_order, 2u);
    CHECK(b.empty());
    check_ok(b);
}

BOOK_TEST(partial_fill_rests_remainder) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 4);
    const auto r = lim(b, t, 2, Side::Buy, 100, 10);
    CHECK(r.status == OrderStatus::PartiallyFilled);
    CHECK_EQ(r.filled, 4);
    CHECK_EQ(r.resting, 6);
    CHECK_EQ(*b.best_bid(), 100);
    CHECK(!b.best_ask());
    CHECK_EQ(b.volume_at(Side::Buy, 100), 6);
    check_ok(b);
}

BOOK_TEST(partial_fill_of_maker_keeps_priority) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 10);
    lim(b, t, 2, Side::Sell, 100, 10);
    lim(b, t, 3, Side::Buy, 100, 4);
    t.clear();
    lim(b, t, 4, Side::Buy, 100, 8);
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(t[0].maker_order, 1u);
    CHECK_EQ(t[0].quantity, 6);
    CHECK_EQ(t[1].maker_order, 2u);
    CHECK_EQ(t[1].quantity, 2);
    check_ok(b);
}

BOOK_TEST(market_order_sweeps_and_never_rests) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 3);
    lim(b, t, 2, Side::Sell, 101, 3);
    const auto r = b.add(OrderRequest::market(3, Side::Buy, 10), t);
    CHECK(r.status == OrderStatus::Cancelled);
    CHECK_EQ(r.filled, 6);
    CHECK_EQ(r.resting, 0);
    CHECK_EQ(t.size(), 2u);
    CHECK(b.empty());
    const auto r2 = b.add(OrderRequest::market(4, Side::Sell, 5), t);
    CHECK(r2.status == OrderStatus::Cancelled);
    CHECK_EQ(r2.filled, 0);
    check_ok(b);
}

BOOK_TEST(ioc_cancels_remainder) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 3);
    lim(b, t, 2, Side::Sell, 102, 3);
    const auto r = lim(b, t, 3, Side::Buy, 101, 10, TimeInForce::IOC);
    CHECK(r.status == OrderStatus::Cancelled);
    CHECK_EQ(r.filled, 3);
    CHECK(!b.best_bid());
    CHECK_EQ(*b.best_ask(), 102);
    check_ok(b);
}

BOOK_TEST(fok_all_or_nothing) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 3);
    lim(b, t, 2, Side::Sell, 101, 3);
    lim(b, t, 3, Side::Sell, 103, 10);
    // 6 available at or below 101: an order for 7 must do nothing.
    auto r = lim(b, t, 4, Side::Buy, 101, 7, TimeInForce::FOK);
    CHECK(r.status == OrderStatus::Cancelled);
    CHECK_EQ(r.filled, 0);
    CHECK(t.empty());
    CHECK_EQ(b.order_count(), 3u);
    r = lim(b, t, 5, Side::Buy, 101, 6, TimeInForce::FOK);
    CHECK(r.status == OrderStatus::Filled);
    CHECK_EQ(t.size(), 2u);
    CHECK_EQ(*b.best_ask(), 103);
    check_ok(b);
}

BOOK_TEST(cancel_removes_order_and_empty_level) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Buy, 100, 5);
    lim(b, t, 2, Side::Buy, 100, 5);
    lim(b, t, 3, Side::Buy, 99, 5);
    CHECK(b.cancel(1));
    CHECK(!b.cancel(1));
    CHECK(!b.cancel(42));
    CHECK_EQ(b.volume_at(Side::Buy, 100), 5);
    CHECK(b.cancel(2));
    CHECK_EQ(*b.best_bid(), 99);
    CHECK_EQ(b.level_count(Side::Buy), 1u);
    CHECK(b.cancel(3));
    CHECK(!b.best_bid());
    check_ok(b);
}

BOOK_TEST(modify_reduce_keeps_priority) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 10);
    lim(b, t, 2, Side::Sell, 100, 10);
    const auto r = b.modify(1, 100, 4, t);
    CHECK(r.status == OrderStatus::Accepted);
    CHECK_EQ(b.volume_at(Side::Sell, 100), 14);
    lim(b, t, 3, Side::Buy, 100, 4);
    CHECK_EQ(t.size(), 1u);
    CHECK_EQ(t[0].maker_order, 1u);
    check_ok(b);
}

BOOK_TEST(modify_increase_loses_priority) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 100, 5);
    lim(b, t, 2, Side::Sell, 100, 5);
    b.modify(1, 100, 8, t);
    lim(b, t, 3, Side::Buy, 100, 5);
    CHECK_EQ(t.size(), 1u);
    CHECK_EQ(t[0].maker_order, 2u);
    check_ok(b);
}

BOOK_TEST(modify_price_can_cross) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Buy, 98, 5);
    lim(b, t, 2, Side::Sell, 100, 3);
    const auto r = b.modify(1, 100, 5, t);
    CHECK(r.status == OrderStatus::PartiallyFilled);
    CHECK_EQ(r.filled, 3);
    CHECK_EQ(t.size(), 1u);
    CHECK_EQ(*b.best_bid(), 100);
    CHECK_EQ(b.volume_at(Side::Buy, 100), 2);
    CHECK(!b.best_ask());
    check_ok(b);
}

BOOK_TEST(rejections_leave_book_unchanged) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Buy, 100, 5);
    CHECK(lim(b, t, 1, Side::Buy, 99, 5).reason == RejectReason::DuplicateOrderId);
    CHECK(lim(b, t, 0, Side::Buy, 99, 5).reason == RejectReason::InvalidOrderId);
    CHECK(lim(b, t, 2, Side::Buy, 99, 0).reason == RejectReason::InvalidQuantity);
    CHECK(lim(b, t, 3, Side::Buy, 99, -1).reason == RejectReason::InvalidQuantity);
    CHECK(lim(b, t, 4, Side::Buy, 0, 5).reason == RejectReason::InvalidPrice);
    CHECK(b.modify(99, 100, 5, t).reason == RejectReason::UnknownOrder);
    CHECK(b.modify(1, 100, 0, t).reason == RejectReason::InvalidQuantity);
    CHECK_EQ(b.order_count(), 1u);
    CHECK(t.empty());
    check_ok(b);
}

BOOK_TEST(depth_quantity_sums_top_levels) {
    Book b;
    std::vector<Trade> t;
    lim(b, t, 1, Side::Sell, 101, 1);
    lim(b, t, 2, Side::Sell, 102, 2);
    lim(b, t, 3, Side::Sell, 103, 4);
    CHECK_EQ(b.depth_quantity(Side::Sell, 1), 1);
    CHECK_EQ(b.depth_quantity(Side::Sell, 2), 3);
    CHECK_EQ(b.depth_quantity(Side::Sell, 10), 7);
    CHECK_EQ(b.top_quantity(Side::Sell), 1);
    CHECK_EQ(b.depth(Side::Sell, 2).size(), 2u);
}

TEST(ladder_rejects_out_of_range_prices) {
    BookConfig cfg;
    cfg.min_price = 100;
    cfg.max_price = 200;
    LadderOrderBook b(cfg);
    std::vector<Trade> t;
    CHECK(b.add(OrderRequest::limit(1, Side::Buy, 99, 1), t).reason == RejectReason::InvalidPrice);
    CHECK(b.add(OrderRequest::limit(2, Side::Buy, 201, 1), t).reason == RejectReason::InvalidPrice);
    CHECK(b.add(OrderRequest::limit(3, Side::Buy, 100, 1), t).ok());
    CHECK(b.add(OrderRequest::limit(4, Side::Sell, 200, 1), t).ok());
    CHECK(b.add(OrderRequest::market(5, Side::Sell, 1), t).status == OrderStatus::Filled);
}

TEST(ladder_best_rescans_after_level_empties) {
    LadderOrderBook b;
    std::vector<Trade> t;
    b.add(OrderRequest::limit(1, Side::Buy, 100, 1), t);
    b.add(OrderRequest::limit(2, Side::Buy, 90, 1), t);
    b.add(OrderRequest::limit(3, Side::Sell, 110, 1), t);
    b.add(OrderRequest::limit(4, Side::Sell, 120, 1), t);
    b.cancel(1);
    CHECK_EQ(*b.best_bid(), 90);
    b.cancel(3);
    CHECK_EQ(*b.best_ask(), 120);
    b.cancel(2);
    b.cancel(4);
    CHECK(!b.best_bid());
    CHECK(!b.best_ask());
    b.add(OrderRequest::limit(5, Side::Buy, 50, 1), t);
    CHECK_EQ(*b.best_bid(), 50);
}
