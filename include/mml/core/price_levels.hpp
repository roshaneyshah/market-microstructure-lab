#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

#include "mml/core/price_level.hpp"
#include "mml/core/types.hpp"

namespace mml {

struct BookConfig {
    // Inclusive tick range. Only the ladder policy enforces it; the map
    // policy accepts any positive price.
    Price min_price{1};
    Price max_price{100'000};
    // Sizing hint for the id index and node pool.
    std::size_t expected_orders{1u << 16};
};

// Policy interface shared by both level containers (one instance per side):
//
//   bool         valid_price(Price) const
//   PriceLevel*  find_or_create(Price)   caller must push an order right after
//   PriceLevel*  find(Price)             nullptr if no active level
//   PriceLevel*  best()                  best-priced active level or nullptr
//   void         erase(PriceLevel*)      level must be empty
//   void         for_each(F)             best to worst; F returns false to stop
//   std::size_t  size() const            number of active levels
//
// "Best" means highest for bids and lowest for asks.

// Red-black tree of levels. O(log L) insert of a new level, O(1) best.
// Unbounded price range, one heap allocation per new level.
template <Side S>
class MapLevels {
    using Compare = std::conditional_t<S == Side::Buy, std::greater<Price>, std::less<Price>>;

public:
    explicit MapLevels(const BookConfig& /*cfg*/) {}

    [[nodiscard]] bool valid_price(Price p) const noexcept { return p > 0; }

    PriceLevel* find_or_create(Price p) {
        auto [it, inserted] = levels_.try_emplace(p);
        if (inserted) {
            it->second.price = p;
        }
        return &it->second;
    }

    [[nodiscard]] PriceLevel* find(Price p) noexcept {
        auto it = levels_.find(p);
        return it == levels_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] const PriceLevel* find(Price p) const noexcept {
        auto it = levels_.find(p);
        return it == levels_.end() ? nullptr : &it->second;
    }

    [[nodiscard]] PriceLevel* best() noexcept {
        return levels_.empty() ? nullptr : &levels_.begin()->second;
    }
    [[nodiscard]] const PriceLevel* best() const noexcept {
        return levels_.empty() ? nullptr : &levels_.begin()->second;
    }

    void erase(PriceLevel* level) { levels_.erase(level->price); }

    template <class F>
    void for_each(F&& f) const {
        for (const auto& kv : levels_) {
            if (!f(kv.second)) {
                return;
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return levels_.size(); }

private:
    std::map<Price, PriceLevel, Compare> levels_;
};

// Flat array indexed by price over a fixed tick range. O(1) access to any
// level, zero allocation after construction, contiguous memory. The cost is
// memory proportional to the range and a linear scan to find the next best
// level when the best one empties (short in practice because books are dense
// near the touch).
template <Side S>
class LadderLevels {
public:
    explicit LadderLevels(const BookConfig& cfg)
        : min_price_(cfg.min_price),
          levels_(static_cast<std::size_t>(cfg.max_price >= cfg.min_price ? cfg.max_price - cfg.min_price + 1 : 1)) {
        for (std::size_t i = 0; i < levels_.size(); ++i) {
            levels_[i].price = min_price_ + static_cast<Price>(i);
        }
    }

    [[nodiscard]] bool valid_price(Price p) const noexcept {
        return p >= min_price_ && p < min_price_ + static_cast<Price>(levels_.size());
    }

    PriceLevel* find_or_create(Price p) noexcept {
        const auto i = index(p);
        PriceLevel& level = levels_[static_cast<std::size_t>(i)];
        if (level.empty()) {
            ++size_;
            if (best_ < 0 || better(i, best_)) {
                best_ = i;
            }
        }
        return &level;
    }

    [[nodiscard]] PriceLevel* find(Price p) noexcept {
        if (!valid_price(p)) {
            return nullptr;
        }
        PriceLevel& level = levels_[static_cast<std::size_t>(index(p))];
        return level.empty() ? nullptr : &level;
    }
    [[nodiscard]] const PriceLevel* find(Price p) const noexcept {
        if (!valid_price(p)) {
            return nullptr;
        }
        const PriceLevel& level = levels_[static_cast<std::size_t>(index(p))];
        return level.empty() ? nullptr : &level;
    }

    [[nodiscard]] PriceLevel* best() noexcept {
        return best_ < 0 ? nullptr : &levels_[static_cast<std::size_t>(best_)];
    }
    [[nodiscard]] const PriceLevel* best() const noexcept {
        return best_ < 0 ? nullptr : &levels_[static_cast<std::size_t>(best_)];
    }

    void erase(PriceLevel* level) noexcept {
        --size_;
        const std::ptrdiff_t i = level - levels_.data();
        if (i != best_) {
            return;
        }
        best_ = size_ == 0 ? -1 : next_active(i);
    }

    template <class F>
    void for_each(F&& f) const {
        if (best_ < 0) {
            return;
        }
        std::size_t seen = 0;
        const auto n = static_cast<std::ptrdiff_t>(levels_.size());
        for (std::ptrdiff_t j = best_; j >= 0 && j < n; j += step()) {
            const PriceLevel& level = levels_[static_cast<std::size_t>(j)];
            if (level.empty()) {
                continue;
            }
            if (!f(level) || ++seen == size_) {
                return;
            }
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
    // Direction from better to worse prices.
    static constexpr std::ptrdiff_t step() noexcept { return S == Side::Buy ? -1 : 1; }

    static constexpr bool better(std::ptrdiff_t a, std::ptrdiff_t b) noexcept {
        return S == Side::Buy ? a > b : a < b;
    }

    [[nodiscard]] std::ptrdiff_t index(Price p) const noexcept { return static_cast<std::ptrdiff_t>(p - min_price_); }

    [[nodiscard]] std::ptrdiff_t next_active(std::ptrdiff_t from) const noexcept {
        const auto n = static_cast<std::ptrdiff_t>(levels_.size());
        for (std::ptrdiff_t j = from + step(); j >= 0 && j < n; j += step()) {
            if (!levels_[static_cast<std::size_t>(j)].empty()) {
                return j;
            }
        }
        return -1;
    }

    Price min_price_;
    std::vector<PriceLevel> levels_;
    std::ptrdiff_t best_{-1};
    std::size_t size_{0};
};

}  // namespace mml
