#include "mml/analytics/stats.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace mml {

double mean(const std::vector<double>& x) {
    if (x.empty()) {
        return 0.0;
    }
    return std::accumulate(x.begin(), x.end(), 0.0) / static_cast<double>(x.size());
}

double stddev(const std::vector<double>& x) {
    if (x.size() < 2) {
        return 0.0;
    }
    const double m = mean(x);
    double ss = 0.0;
    for (double v : x) {
        ss += (v - m) * (v - m);
    }
    return std::sqrt(ss / static_cast<double>(x.size() - 1));
}

double quantile_sorted(const std::vector<double>& sorted, double q) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double pos = std::clamp(q, 0.0, 1.0) * static_cast<double>(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(std::floor(pos));
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    const double frac = pos - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}

Summary summarize(std::vector<double> xs) {
    Summary s;
    s.n = xs.size();
    if (xs.empty()) {
        return s;
    }
    std::sort(xs.begin(), xs.end());
    s.mean = mean(xs);
    s.stddev = stddev(xs);
    s.min = xs.front();
    s.max = xs.back();
    s.p05 = quantile_sorted(xs, 0.05);
    s.p25 = quantile_sorted(xs, 0.25);
    s.median = quantile_sorted(xs, 0.50);
    s.p75 = quantile_sorted(xs, 0.75);
    s.p95 = quantile_sorted(xs, 0.95);
    // Worst 5% of observations, at least one.
    const std::size_t tail = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(0.05 * static_cast<double>(xs.size()))));
    double sum = 0.0;
    for (std::size_t i = xs.size() - tail; i < xs.size(); ++i) {
        sum += xs[i];
    }
    s.cvar95 = sum / static_cast<double>(tail);
    return s;
}

OlsFit ols(const std::vector<double>& x, const std::vector<double>& y) {
    OlsFit fit;
    const std::size_t n = std::min(x.size(), y.size());
    fit.n = n;
    if (n < 2) {
        return fit;
    }
    double mx = 0.0, my = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxx = 0.0, sxy = 0.0, syy = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        sxx += (x[i] - mx) * (x[i] - mx);
        sxy += (x[i] - mx) * (y[i] - my);
        syy += (y[i] - my) * (y[i] - my);
    }
    if (sxx <= 0.0) {
        fit.intercept = my;
        return fit;
    }
    fit.slope = sxy / sxx;
    fit.intercept = my - fit.slope * mx;
    fit.r2 = syy > 0.0 ? (sxy * sxy) / (sxx * syy) : 0.0;
    return fit;
}

double slope_through_origin(const std::vector<double>& x, const std::vector<double>& y) {
    double sxy = 0.0, sxx = 0.0;
    const std::size_t n = std::min(x.size(), y.size());
    for (std::size_t i = 0; i < n; ++i) {
        sxy += x[i] * y[i];
        sxx += x[i] * x[i];
    }
    return sxx > 0.0 ? sxy / sxx : 0.0;
}

double correlation(const std::vector<double>& x, const std::vector<double>& y) {
    const OlsFit f = ols(x, y);
    const double r = std::sqrt(f.r2);
    return f.slope >= 0.0 ? r : -r;
}

}  // namespace mml
