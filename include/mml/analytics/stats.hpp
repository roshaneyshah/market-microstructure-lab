#pragma once

#include <cstddef>
#include <vector>

namespace mml {

// Distribution summary. Tail statistics treat larger values as worse, which
// matches how costs (implementation shortfall, slippage) are reported.
struct Summary {
    std::size_t n{0};
    double mean{0.0};
    double stddev{0.0};
    double min{0.0};
    double p05{0.0};
    double p25{0.0};
    double median{0.0};
    double p75{0.0};
    double p95{0.0};
    double max{0.0};
    double cvar95{0.0};  // mean of the worst 5% (values at or above p95)
};

[[nodiscard]] Summary summarize(std::vector<double> xs);

// Linear interpolation quantile of an already sorted sample, q in [0, 1].
[[nodiscard]] double quantile_sorted(const std::vector<double>& sorted, double q);

struct OlsFit {
    double intercept{0.0};
    double slope{0.0};
    double r2{0.0};
    std::size_t n{0};
};

[[nodiscard]] OlsFit ols(const std::vector<double>& x, const std::vector<double>& y);
[[nodiscard]] double slope_through_origin(const std::vector<double>& x, const std::vector<double>& y);
[[nodiscard]] double correlation(const std::vector<double>& x, const std::vector<double>& y);
[[nodiscard]] double mean(const std::vector<double>& x);
[[nodiscard]] double stddev(const std::vector<double>& x);

}  // namespace mml
