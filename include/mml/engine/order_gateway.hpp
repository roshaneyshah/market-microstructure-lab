#pragma once

#include <vector>

#include "mml/core/order.hpp"
#include "mml/engine/matching_engine.hpp"

namespace mml {

// Execution report delivered to a participant for its side of a trade.
struct Fill {
    OrderId order{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    bool maker{false};
    // Mid price immediately before the message that caused this fill. Used for
    // spread-capture and adverse-selection accounting.
    double reference_mid{0.0};
    Timestamp ts{0};
};

class FillListener {
public:
    virtual ~FillListener() = default;
    // Must not submit orders re-entrantly from inside this callback.
    virtual void on_fill(const Fill& fill) = 0;
};

struct RiskLimits {
    Quantity max_order_quantity{1'000'000};
};

struct SubmitResult {
    OrderId id{kNoOrder};
    ExecResult result{};
};

// Entry point for participants. Assigns order ids, applies pre-trade risk
// checks, enforces order ownership on cancel/modify, and routes fills to the
// owning participants on both sides of every trade.
class OrderGateway {
public:
    explicit OrderGateway(MatchingEngine& engine, RiskLimits limits = {});

    // Returns a new trader id. `listener` may be null for participants that do
    // not need fill callbacks.
    TraderId register_trader(FillListener* listener = nullptr);

    SubmitResult limit(TraderId trader, Side side, Price price, Quantity qty, TimeInForce tif = TimeInForce::GTC);
    SubmitResult market(TraderId trader, Side side, Quantity qty);
    bool cancel(TraderId trader, OrderId id);
    SubmitResult modify(TraderId trader, OrderId id, Price new_price, Quantity new_quantity);

    // Last observed two-sided mid (falls back to the previous value while one
    // side is empty).
    [[nodiscard]] double reference_mid() const noexcept { return last_mid_; }
    void set_reference_mid(double mid) noexcept { last_mid_ = mid; }

    [[nodiscard]] const MatchingEngine& engine() const noexcept { return engine_; }
    [[nodiscard]] const OrderBook& book() const noexcept { return engine_.book(); }

private:
    SubmitResult send(const OrderRequest& req);
    void dispatch(double mid_before);
    void refresh_mid() noexcept;

    MatchingEngine& engine_;
    RiskLimits limits_;
    std::vector<FillListener*> listeners_;
    OrderId next_id_{1};
    double last_mid_{0.0};
};

}  // namespace mml
