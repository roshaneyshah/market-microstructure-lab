// Guided walkthrough of the exchange core: resting orders, price-time
// priority, partial fills, market/IOC/FOK orders, cancel and modify.

#include <cstdio>
#include <string>
#include <vector>

#include "mml/engine/matching_engine.hpp"
#include "mml/engine/order_gateway.hpp"

using namespace mml;

namespace {

void print_book(const OrderBook& book, std::size_t levels = 5) {
    const auto asks = book.depth(Side::Sell, levels);
    const auto bids = book.depth(Side::Buy, levels);
    std::printf("    %8s %8s %6s\n", "price", "qty", "orders");
    for (auto it = asks.rbegin(); it != asks.rend(); ++it) {
        std::printf("ASK %8lld %8lld %6u\n", static_cast<long long>(it->price), static_cast<long long>(it->quantity),
                    it->orders);
    }
    std::printf("    ------------------------\n");
    for (const auto& l : bids) {
        std::printf("BID %8lld %8lld %6u\n", static_cast<long long>(l.price), static_cast<long long>(l.quantity),
                    l.orders);
    }
    std::printf("\n");
}

struct Printer : FillListener {
    const char* name;
    explicit Printer(const char* n) : name(n) {}
    void on_fill(const Fill& f) override {
        std::printf("  fill -> %-6s order %-3llu %-4s %3lld @ %lld (%s)\n", name,
                    static_cast<unsigned long long>(f.order), f.side == Side::Buy ? "BUY" : "SELL",
                    static_cast<long long>(f.quantity), static_cast<long long>(f.price), f.maker ? "maker" : "taker");
    }
};

void show(const char* title, const SubmitResult& r) {
    std::printf("%s\n  order %llu: %s filled=%lld resting=%lld\n", title, static_cast<unsigned long long>(r.id),
                std::string(to_string(r.result.status)).c_str(), static_cast<long long>(r.result.filled),
                static_cast<long long>(r.result.resting));
}

}  // namespace

int main() {
    MatchingEngine engine;
    OrderGateway gw(engine);
    Printer alice_p("alice"), bob_p("bob"), carol_p("carol");
    const TraderId alice = gw.register_trader(&alice_p);
    const TraderId bob = gw.register_trader(&bob_p);
    const TraderId carol = gw.register_trader(&carol_p);

    std::printf("== 1. Build a book\n");
    gw.limit(alice, Side::Buy, 99, 10);
    gw.limit(alice, Side::Buy, 98, 20);
    const auto a1 = gw.limit(alice, Side::Sell, 101, 5);
    gw.limit(bob, Side::Sell, 101, 7);  // same price, later: queues behind alice
    gw.limit(bob, Side::Sell, 102, 15);
    print_book(engine.book());

    std::printf("== 2. Price-time priority: carol buys 8 at 101\n");
    show("limit buy 8 @ 101", gw.limit(carol, Side::Buy, 101, 8));
    print_book(engine.book());

    std::printf("== 3. Marketable limit with a remainder that rests\n");
    show("limit buy 20 @ 102", gw.limit(carol, Side::Buy, 102, 20));
    print_book(engine.book());

    std::printf("== 4. Market sell sweeps several levels\n");
    show("market sell 25", gw.market(bob, Side::Sell, 25));
    print_book(engine.book());

    std::printf("== 5. IOC and FOK\n");
    gw.limit(alice, Side::Sell, 100, 4);
    show("IOC buy 10 @ 100 (only 4 available)", gw.limit(carol, Side::Buy, 100, 10, TimeInForce::IOC));
    gw.limit(alice, Side::Sell, 100, 4);
    show("FOK buy 10 @ 100 (only 4 available)", gw.limit(carol, Side::Buy, 100, 10, TimeInForce::FOK));
    print_book(engine.book());

    std::printf("== 6. Modify and cancel\n");
    const auto r = gw.limit(alice, Side::Buy, 97, 10);
    show("new buy 10 @ 97", r);
    show("modify to 6 @ 97 (keeps priority)", gw.modify(alice, r.id, 97, 6));
    show("modify to 6 @ 99 (new price, loses priority)", gw.modify(alice, r.id, 99, 6));
    std::printf("cancel of already-filled order %llu -> %s\n", static_cast<unsigned long long>(a1.id),
                gw.cancel(alice, a1.id) ? "ok" : "rejected");
    print_book(engine.book());

    const auto& st = engine.stats();
    std::printf("trades=%llu volume=%lld vwap=%.2f\n", static_cast<unsigned long long>(st.trades),
                static_cast<long long>(st.volume), st.volume > 0 ? st.notional / static_cast<double>(st.volume) : 0.0);
    return 0;
}
