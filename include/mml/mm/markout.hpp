#pragma once

#include <vector>

#include "mml/mm/market_maker.hpp"
#include "mml/sim/market_simulator.hpp"

namespace mml {

// Adverse-selection decomposition of fills at a given horizon. For a fill at
// price p with reference mid m, and mid m_h at the end of step (fill step + h):
//
//   spread capture  side * (m   - p)   what the quote earned against the mid
//   adverse move    side * (m_h - m)   how the mid moved after the fill
//   markout         side * (m_h - p)   = spread capture + adverse move
//
// Values are quantity-weighted averages per unit, in ticks. A negative
// adverse move means fills tend to precede prices moving against the maker.
struct MarkoutStats {
    std::size_t horizon{0};
    std::size_t fills{0};
    double spread_capture{0.0};
    double adverse_move{0.0};
    double markout{0.0};
};

[[nodiscard]] std::vector<MarkoutStats> compute_markouts(const std::vector<FillRecord>& fills,
                                                         const std::vector<StepRecord>& history,
                                                         const std::vector<std::size_t>& horizons,
                                                         bool maker_only = true);

}  // namespace mml
