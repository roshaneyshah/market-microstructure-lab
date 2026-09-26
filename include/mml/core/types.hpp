#pragma once

#include <cstdint>
#include <string_view>

namespace mml {

// Prices are integer ticks. Converting to currency is a presentation concern
// (price * tick_size) and never happens inside the exchange core, which keeps
// matching exact and free of floating point comparisons.
using Price = std::int64_t;
using Quantity = std::int64_t;
using OrderId = std::uint64_t;
using TraderId = std::uint32_t;
using Timestamp = std::uint64_t;

inline constexpr OrderId kNoOrder = 0;
inline constexpr TraderId kNoTrader = 0;

enum class Side : std::uint8_t { Buy = 0, Sell = 1 };
enum class OrderType : std::uint8_t { Limit, Market };

// GTC: rest any unfilled remainder.
// IOC: fill what is possible immediately, cancel the rest.
// FOK: fill the entire quantity immediately or do nothing.
enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

[[nodiscard]] constexpr Side opposite(Side s) noexcept {
    return s == Side::Buy ? Side::Sell : Side::Buy;
}

// +1 for buys, -1 for sells. Used throughout for signed quantities and PnL.
[[nodiscard]] constexpr int sign(Side s) noexcept { return s == Side::Buy ? 1 : -1; }

[[nodiscard]] constexpr std::string_view to_string(Side s) noexcept {
    return s == Side::Buy ? "BUY" : "SELL";
}

[[nodiscard]] constexpr std::string_view to_string(OrderType t) noexcept {
    return t == OrderType::Limit ? "LIMIT" : "MARKET";
}

[[nodiscard]] constexpr std::string_view to_string(TimeInForce t) noexcept {
    switch (t) {
        case TimeInForce::GTC: return "GTC";
        case TimeInForce::IOC: return "IOC";
        case TimeInForce::FOK: return "FOK";
    }
    return "?";
}

}  // namespace mml
