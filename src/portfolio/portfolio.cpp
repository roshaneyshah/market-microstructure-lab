#include "mml/portfolio/portfolio.hpp"

#include <algorithm>
#include <cstdlib>

namespace mml {

void Portfolio::mark(double mid) {
    if (has_mid_) {
        attribution_.inventory += static_cast<double>(position_) * (mid - last_mid_);
    }
    last_mid_ = mid;
    has_mid_ = true;
}

void Portfolio::on_fill(Side side, Price price, Quantity qty, bool maker, double mid) {
    mark(mid);

    const int s = sign(side);
    const double px = static_cast<double>(price);
    const double q = static_cast<double>(qty);
    const double fee = (maker ? fees_.maker_fee : fees_.taker_fee) * q;

    attribution_.spread_capture += s * (mid - px) * q;
    attribution_.fees += fee;
    cash_ -= s * px * q + fee;

    update_cost_basis(s, price, qty);
    position_ += s * qty;

    ++stats_.fills;
    if (maker) {
        ++stats_.maker_fills;
    }
    stats_.volume += qty;
    stats_.max_abs_position = std::max(stats_.max_abs_position, std::abs(position_));
}

void Portfolio::update_cost_basis(int s, Price price, Quantity qty) {
    const double px = static_cast<double>(price);
    const Quantity signed_qty = s * qty;
    if (position_ == 0 || (position_ > 0) == (signed_qty > 0)) {
        const double abs_pos = static_cast<double>(std::abs(position_));
        avg_cost_ = (avg_cost_ * abs_pos + px * static_cast<double>(qty)) / (abs_pos + static_cast<double>(qty));
        return;
    }
    const Quantity closing = std::min(qty, std::abs(position_));
    const double pos_sign = position_ > 0 ? 1.0 : -1.0;
    realized_ += static_cast<double>(closing) * (px - avg_cost_) * pos_sign;
    if (qty > std::abs(position_)) {
        avg_cost_ = px;  // flipped: remainder opens a new position at this price
    } else if (qty == std::abs(position_)) {
        avg_cost_ = 0.0;
    }
}

}  // namespace mml
