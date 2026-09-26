#pragma once

#include <string_view>

#include "mml/core/types.hpp"

namespace mml {

// An instruction sent to the book. For market orders `price` is ignored.
struct OrderRequest {
    OrderId id{kNoOrder};
    TraderId trader{kNoTrader};
    Side side{Side::Buy};
    OrderType type{OrderType::Limit};
    TimeInForce tif{TimeInForce::GTC};
    Price price{0};
    Quantity quantity{0};
    Timestamp ts{0};

    [[nodiscard]] static OrderRequest limit(OrderId id, Side side, Price price, Quantity qty,
                                            TimeInForce tif = TimeInForce::GTC,
                                            TraderId trader = kNoTrader) noexcept {
        return OrderRequest{id, trader, side, OrderType::Limit, tif, price, qty, 0};
    }

    [[nodiscard]] static OrderRequest market(OrderId id, Side side, Quantity qty,
                                             TraderId trader = kNoTrader) noexcept {
        return OrderRequest{id, trader, side, OrderType::Market, TimeInForce::IOC, 0, qty, 0};
    }
};

// One execution between a resting (maker) order and an incoming (taker) order.
// Trades always print at the maker's price.
struct Trade {
    OrderId maker_order{kNoOrder};
    OrderId taker_order{kNoOrder};
    TraderId maker_trader{kNoTrader};
    TraderId taker_trader{kNoTrader};
    Side aggressor{Side::Buy};
    Price price{0};
    Quantity quantity{0};
    Timestamp ts{0};

    friend bool operator==(const Trade&, const Trade&) = default;
};

enum class OrderStatus : std::uint8_t {
    Accepted,         // rested in the book without any fill
    PartiallyFilled,  // some quantity filled, remainder rests in the book
    Filled,           // fully filled
    Cancelled,        // remainder not rested (IOC, FOK, market); may have partial fills
    Rejected,         // failed validation, book unchanged
};

enum class RejectReason : std::uint8_t {
    None,
    InvalidOrderId,
    DuplicateOrderId,
    InvalidQuantity,
    InvalidPrice,
    UnknownOrder,
    RiskLimit,
    NotOwner,
};

struct ExecResult {
    OrderStatus status{OrderStatus::Rejected};
    RejectReason reason{RejectReason::None};
    Quantity filled{0};
    Quantity resting{0};

    [[nodiscard]] bool ok() const noexcept { return status != OrderStatus::Rejected; }
};

[[nodiscard]] constexpr std::string_view to_string(OrderStatus s) noexcept {
    switch (s) {
        case OrderStatus::Accepted: return "ACCEPTED";
        case OrderStatus::PartiallyFilled: return "PARTIALLY_FILLED";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "?";
}

[[nodiscard]] constexpr std::string_view to_string(RejectReason r) noexcept {
    switch (r) {
        case RejectReason::None: return "NONE";
        case RejectReason::InvalidOrderId: return "INVALID_ORDER_ID";
        case RejectReason::DuplicateOrderId: return "DUPLICATE_ORDER_ID";
        case RejectReason::InvalidQuantity: return "INVALID_QUANTITY";
        case RejectReason::InvalidPrice: return "INVALID_PRICE";
        case RejectReason::UnknownOrder: return "UNKNOWN_ORDER";
        case RejectReason::RiskLimit: return "RISK_LIMIT";
        case RejectReason::NotOwner: return "NOT_OWNER";
    }
    return "?";
}

}  // namespace mml
