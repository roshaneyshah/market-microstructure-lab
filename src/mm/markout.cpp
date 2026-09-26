#include "mml/mm/markout.hpp"

namespace mml {

std::vector<MarkoutStats> compute_markouts(const std::vector<FillRecord>& fills, const std::vector<StepRecord>& history,
                                           const std::vector<std::size_t>& horizons, bool maker_only) {
    std::vector<MarkoutStats> out;
    out.reserve(horizons.size());
    for (std::size_t h : horizons) {
        MarkoutStats m;
        m.horizon = h;
        double qty = 0.0;
        for (const FillRecord& f : fills) {
            if (maker_only && !f.maker) {
                continue;
            }
            const std::size_t at = f.step + h;
            if (at >= history.size()) {
                continue;
            }
            const double s = sign(f.side);
            const double q = static_cast<double>(f.quantity);
            const double px = static_cast<double>(f.price);
            m.spread_capture += s * (f.mid - px) * q;
            m.adverse_move += s * (history[at].mid - f.mid) * q;
            qty += q;
            ++m.fills;
        }
        if (qty > 0.0) {
            m.spread_capture /= qty;
            m.adverse_move /= qty;
        }
        m.markout = m.spread_capture + m.adverse_move;
        out.push_back(m);
    }
    return out;
}

}  // namespace mml
