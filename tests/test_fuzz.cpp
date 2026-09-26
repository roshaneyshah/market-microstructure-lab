// Differential fuzzing: random order streams are replayed into all four book
// variants and a deliberately naive reference implementation. Every trade and
// every depth snapshot must agree, and structural invariants must hold after
// every operation.

#include <algorithm>
#include <string>
#include <vector>

#include "mml/core/order_book.hpp"
#include "mml/sim/random.hpp"
#include "test_framework.hpp"

using namespace mml;

namespace {

// O(n) per operation, obviously correct: scan every resting order for the
// best price, breaking ties by arrival sequence.
class ReferenceBook {
public:
    ExecResult add(const OrderRequest& req, std::vector<Trade>& trades) {
        if (req.id == kNoOrder) return {OrderStatus::Rejected, RejectReason::InvalidOrderId, 0, 0};
        if (req.quantity <= 0) return {OrderStatus::Rejected, RejectReason::InvalidQuantity, 0, 0};
        if (req.type == OrderType::Limit && req.price <= 0)
            return {OrderStatus::Rejected, RejectReason::InvalidPrice, 0, 0};
        if (find(req.id) != orders_.end()) return {OrderStatus::Rejected, RejectReason::DuplicateOrderId, 0, 0};

        auto crosses = [&](const Resting& o) {
            if (o.side == req.side) return false;
            if (req.type == OrderType::Market) return true;
            return req.side == Side::Buy ? o.price <= req.price : o.price >= req.price;
        };
        if (req.tif == TimeInForce::FOK) {
            Quantity avail = 0;
            for (const auto& o : orders_) {
                if (crosses(o)) avail += o.qty;
            }
            if (avail < req.quantity) return {OrderStatus::Cancelled, RejectReason::None, 0, 0};
        }
        Quantity left = req.quantity;
        while (left > 0) {
            auto best = orders_.end();
            for (auto it = orders_.begin(); it != orders_.end(); ++it) {
                if (!crosses(*it)) continue;
                if (best == orders_.end()) {
                    best = it;
                    continue;
                }
                const bool better_px = req.side == Side::Buy ? it->price < best->price : it->price > best->price;
                if (better_px || (it->price == best->price && it->seq < best->seq)) best = it;
            }
            if (best == orders_.end()) break;
            const Quantity q = std::min(left, best->qty);
            trades.push_back(Trade{best->id, req.id, best->trader, req.trader, req.side, best->price, q, req.ts});
            left -= q;
            best->qty -= q;
            if (best->qty == 0) orders_.erase(best);
        }
        const Quantity filled = req.quantity - left;
        if (left == 0) return {OrderStatus::Filled, RejectReason::None, filled, 0};
        if (req.type == OrderType::Limit && req.tif == TimeInForce::GTC) {
            orders_.push_back(Resting{req.id, req.trader, req.side, req.price, left, ++seq_});
            return {filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Accepted, RejectReason::None, filled,
                    left};
        }
        return {OrderStatus::Cancelled, RejectReason::None, filled, 0};
    }

    bool cancel(OrderId id) {
        auto it = find(id);
        if (it == orders_.end()) return false;
        orders_.erase(it);
        return true;
    }

    ExecResult modify(OrderId id, Price price, Quantity qty, std::vector<Trade>& trades) {
        auto it = find(id);
        if (it == orders_.end()) return {OrderStatus::Rejected, RejectReason::UnknownOrder, 0, 0};
        if (qty <= 0) return {OrderStatus::Rejected, RejectReason::InvalidQuantity, 0, 0};
        if (price == it->price && qty <= it->qty) {
            it->qty = qty;
            return {OrderStatus::Accepted, RejectReason::None, 0, qty};
        }
        if (price <= 0) return {OrderStatus::Rejected, RejectReason::InvalidPrice, 0, 0};
        const Resting old = *it;
        orders_.erase(it);
        return add(OrderRequest::limit(id, old.side, price, qty, TimeInForce::GTC, old.trader), trades);
    }

    std::vector<LevelSnapshot> depth(Side side, std::size_t n) const {
        std::vector<LevelSnapshot> levels;
        for (const auto& o : orders_) {
            if (o.side != side) continue;
            auto it = std::find_if(levels.begin(), levels.end(), [&](const auto& l) { return l.price == o.price; });
            if (it == levels.end()) {
                levels.push_back({o.price, o.qty, 1});
            } else {
                it->quantity += o.qty;
                ++it->orders;
            }
        }
        std::sort(levels.begin(), levels.end(), [&](const auto& a, const auto& b) {
            return side == Side::Buy ? a.price > b.price : a.price < b.price;
        });
        if (levels.size() > n) levels.resize(n);
        return levels;
    }

    std::size_t size() const { return orders_.size(); }

private:
    struct Resting {
        OrderId id;
        TraderId trader;
        Side side;
        Price price;
        Quantity qty;
        std::uint64_t seq;
    };
    std::vector<Resting>::iterator find(OrderId id) {
        return std::find_if(orders_.begin(), orders_.end(), [&](const auto& o) { return o.id == id; });
    }
    std::vector<Resting> orders_;
    std::uint64_t seq_{0};
};

bool same(const ExecResult& a, const ExecResult& b) {
    return a.status == b.status && a.reason == b.reason && a.filled == b.filled && a.resting == b.resting;
}

void run_fuzz(std::uint64_t seed, int ops) {
    BookConfig cfg;
    cfg.min_price = 1;
    cfg.max_price = 400;
    OrderBook b1(cfg);
    MapDenseOrderBook b2(cfg);
    LadderHashOrderBook b3(cfg);
    LadderOrderBook b4(cfg);
    ReferenceBook ref;

    Rng rng(seed);
    OrderId next_id = 1;
    std::vector<OrderId> ids;
    std::vector<Trade> t1, t2, t3, t4, tr;

    for (int i = 0; i < ops; ++i) {
        t1.clear(); t2.clear(); t3.clear(); t4.clear(); tr.clear();
        const double u = rng.uniform();
        ExecResult r1, r2, r3, r4, rr;
        if (u < 0.55 || ids.empty()) {
            const Side side = rng.bernoulli(0.5) ? Side::Buy : Side::Sell;
            const Price px = 200 + (side == Side::Buy ? -1 : 1) * rng.uniform_int(-3, 12);
            const Quantity qty = rng.uniform_int(1, 20);
            const double k = rng.uniform();
            const TimeInForce tif = k < 0.8 ? TimeInForce::GTC : (k < 0.9 ? TimeInForce::IOC : TimeInForce::FOK);
            OrderRequest req = OrderRequest::limit(next_id++, side, px, qty, tif, 1);
            if (rng.bernoulli(0.08)) req = OrderRequest::market(req.id, side, qty, 1);
            if (rng.bernoulli(0.01)) req.quantity = 0;  // exercise rejections
            r1 = b1.add(req, t1); r2 = b2.add(req, t2); r3 = b3.add(req, t3); r4 = b4.add(req, t4);
            rr = ref.add(req, tr);
            ids.push_back(req.id);
        } else if (u < 0.85) {
            const auto idx = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::int64_t>(ids.size()) - 1));
            const OrderId id = ids[idx];
            const bool c1 = b1.cancel(id), c2 = b2.cancel(id), c3 = b3.cancel(id), c4 = b4.cancel(id);
            const bool cr = ref.cancel(id);
            CHECK(c1 == cr && c2 == cr && c3 == cr && c4 == cr);
            ids[idx] = ids.back();
            ids.pop_back();
            continue;
        } else {
            const auto idx = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::int64_t>(ids.size()) - 1));
            const OrderId id = ids[idx];
            const Price px = 200 + rng.uniform_int(-10, 10);
            const Quantity qty = rng.uniform_int(1, 25);
            r1 = b1.modify(id, px, qty, t1); r2 = b2.modify(id, px, qty, t2);
            r3 = b3.modify(id, px, qty, t3); r4 = b4.modify(id, px, qty, t4);
            rr = ref.modify(id, px, qty, tr);
        }
        CHECK(same(r1, rr) && same(r2, rr) && same(r3, rr) && same(r4, rr));
        CHECK(t1 == tr && t2 == tr && t3 == tr && t4 == tr);
        if (i % 7 == 0) {
            for (Side s : {Side::Buy, Side::Sell}) {
                const auto d = ref.depth(s, 1000);
                CHECK(b1.depth(s, 1000) == d);
                CHECK(b2.depth(s, 1000) == d);
                CHECK(b3.depth(s, 1000) == d);
                CHECK(b4.depth(s, 1000) == d);
            }
            std::string why;
            CHECK(b1.check_invariants(&why));
            CHECK(b2.check_invariants(&why));
            CHECK(b3.check_invariants(&why));
            CHECK(b4.check_invariants(&why));
        }
        CHECK_EQ(b1.order_count(), ref.size());
        CHECK_EQ(b4.order_count(), ref.size());
    }
}

}  // namespace

TEST(fuzz_books_match_reference_implementation) {
    for (std::uint64_t seed = 1; seed <= 12; ++seed) {
        run_fuzz(seed, 3000);
    }
}
