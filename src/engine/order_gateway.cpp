#include "mml/engine/order_gateway.hpp"

namespace mml {

OrderGateway::OrderGateway(MatchingEngine& engine, RiskLimits limits) : engine_(engine), limits_(limits) {
    listeners_.push_back(nullptr);  // trader id 0 is reserved
    refresh_mid();
}

TraderId OrderGateway::register_trader(FillListener* listener) {
    listeners_.push_back(listener);
    return static_cast<TraderId>(listeners_.size() - 1);
}

SubmitResult OrderGateway::limit(TraderId trader, Side side, Price price, Quantity qty, TimeInForce tif) {
    return send(OrderRequest::limit(next_id_++, side, price, qty, tif, trader));
}

SubmitResult OrderGateway::market(TraderId trader, Side side, Quantity qty) {
    return send(OrderRequest::market(next_id_++, side, qty, trader));
}

bool OrderGateway::cancel(TraderId trader, OrderId id) {
    const OrderNode* node = engine_.book().find(id);
    if (node == nullptr || node->trader != trader) {
        return false;
    }
    const bool ok = engine_.cancel(id);
    refresh_mid();
    return ok;
}

SubmitResult OrderGateway::modify(TraderId trader, OrderId id, Price new_price, Quantity new_quantity) {
    const OrderNode* node = engine_.book().find(id);
    if (node == nullptr) {
        return SubmitResult{id, ExecResult{OrderStatus::Rejected, RejectReason::UnknownOrder, 0, 0}};
    }
    if (node->trader != trader) {
        return SubmitResult{id, ExecResult{OrderStatus::Rejected, RejectReason::NotOwner, 0, 0}};
    }
    if (new_quantity > limits_.max_order_quantity) {
        return SubmitResult{id, ExecResult{OrderStatus::Rejected, RejectReason::RiskLimit, 0, 0}};
    }
    const double mid_before = last_mid_;
    const ExecResult r = engine_.modify(id, new_price, new_quantity);
    dispatch(mid_before);
    refresh_mid();
    return SubmitResult{id, r};
}

SubmitResult OrderGateway::send(const OrderRequest& req) {
    if (req.quantity > limits_.max_order_quantity) {
        return SubmitResult{req.id, ExecResult{OrderStatus::Rejected, RejectReason::RiskLimit, 0, 0}};
    }
    const double mid_before = last_mid_;
    const ExecResult r = engine_.submit(req);
    dispatch(mid_before);
    refresh_mid();
    return SubmitResult{req.id, r};
}

void OrderGateway::dispatch(double mid_before) {
    for (const Trade& t : engine_.last_trades()) {
        if (t.maker_trader < listeners_.size() && listeners_[t.maker_trader] != nullptr) {
            listeners_[t.maker_trader]->on_fill(Fill{t.maker_order, t.maker_trader, opposite(t.aggressor), t.price,
                                                     t.quantity, true, mid_before, t.ts});
        }
        if (t.taker_trader < listeners_.size() && listeners_[t.taker_trader] != nullptr) {
            listeners_[t.taker_trader]->on_fill(
                Fill{t.taker_order, t.taker_trader, t.aggressor, t.price, t.quantity, false, mid_before, t.ts});
        }
    }
}

void OrderGateway::refresh_mid() noexcept {
    if (const auto m = engine_.book().mid()) {
        last_mid_ = *m;
    }
}

}  // namespace mml
