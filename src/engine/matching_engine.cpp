#include "mml/engine/matching_engine.hpp"

namespace mml {

MatchingEngine::MatchingEngine(const BookConfig& cfg) : book_(cfg) { last_trades_.reserve(64); }

ExecResult MatchingEngine::submit(OrderRequest req) {
    req.ts = ++clock_;
    last_trades_.clear();
    const ExecResult r = book_.add(req, last_trades_);
    ++stats_.orders;
    record(r);
    return r;
}

bool MatchingEngine::cancel(OrderId id) {
    ++clock_;
    last_trades_.clear();
    const bool ok = book_.cancel(id);
    if (ok) {
        ++stats_.cancels;
    }
    return ok;
}

ExecResult MatchingEngine::modify(OrderId id, Price new_price, Quantity new_quantity) {
    const Timestamp ts = ++clock_;
    last_trades_.clear();
    const ExecResult r = book_.modify(id, new_price, new_quantity, last_trades_, ts);
    record(r);
    return r;
}

void MatchingEngine::record(const ExecResult& r) {
    if (!r.ok()) {
        ++stats_.rejects;
    }
    for (const Trade& t : last_trades_) {
        stats_.volume += t.quantity;
        stats_.signed_volume += sign(t.aggressor) * t.quantity;
        stats_.notional += static_cast<double>(t.price) * static_cast<double>(t.quantity);
        ++stats_.trades;
    }
    if (keep_tape_) {
        tape_.insert(tape_.end(), last_trades_.begin(), last_trades_.end());
    }
}

}  // namespace mml
