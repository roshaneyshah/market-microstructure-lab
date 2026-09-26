#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

namespace mml {

// xoshiro256** seeded through splitmix64, with hand-written distributions.
// std:: distributions are implementation defined, so they would make results
// differ between libstdc++, libc++ and MSVC. Everything here is bit-for-bit
// reproducible across platforms for a given seed.
class Rng {
public:
    explicit Rng(std::uint64_t seed = 1) noexcept { reseed(seed); }

    void reseed(std::uint64_t seed) noexcept {
        std::uint64_t x = seed;
        for (auto& s : s_) {
            s = splitmix64(x);
        }
        has_spare_ = false;
    }

    std::uint64_t next() noexcept {
        const std::uint64_t result = rotl(s_[1] * 5, 7) * 9;
        const std::uint64_t t = s_[1] << 17;
        s_[2] ^= s_[0];
        s_[3] ^= s_[1];
        s_[1] ^= s_[2];
        s_[0] ^= s_[3];
        s_[2] ^= t;
        s_[3] = rotl(s_[3], 45);
        return result;
    }

    // Uniform on [0, 1).
    double uniform() noexcept { return static_cast<double>(next() >> 11) * 0x1.0p-53; }
    double uniform(double a, double b) noexcept { return a + (b - a) * uniform(); }

    // Uniform integer on [lo, hi].
    std::int64_t uniform_int(std::int64_t lo, std::int64_t hi) noexcept {
        if (hi <= lo) {
            return lo;
        }
        const auto range = static_cast<std::uint64_t>(hi - lo) + 1;
        return lo + static_cast<std::int64_t>(static_cast<std::uint64_t>(uniform() * static_cast<double>(range)) %
                                              range);
    }

    bool bernoulli(double p) noexcept { return uniform() < p; }

    // Standard normal via Box-Muller.
    double normal() noexcept {
        if (has_spare_) {
            has_spare_ = false;
            return spare_;
        }
        double u1 = uniform();
        while (u1 <= 0.0) {
            u1 = uniform();
        }
        const double u2 = uniform();
        const double r = std::sqrt(-2.0 * std::log(u1));
        const double theta = 2.0 * std::numbers::pi * u2;
        spare_ = r * std::sin(theta);
        has_spare_ = true;
        return r * std::cos(theta);
    }

    double normal(double mean, double sd) noexcept { return mean + sd * normal(); }

    double exponential(double rate) noexcept {
        double u = uniform();
        while (u <= 0.0) {
            u = uniform();
        }
        return -std::log(u) / rate;
    }

    // Poisson counts. Knuth's product method for small means, a rounded
    // normal approximation for large ones.
    std::uint64_t poisson(double lambda) noexcept {
        if (lambda <= 0.0) {
            return 0;
        }
        if (lambda > 30.0) {
            const double x = std::round(normal(lambda, std::sqrt(lambda)));
            return x < 0.0 ? 0 : static_cast<std::uint64_t>(x);
        }
        const double limit = std::exp(-lambda);
        std::uint64_t k = 0;
        double p = uniform();
        while (p > limit) {
            ++k;
            p *= uniform();
        }
        return k;
    }

    // Number of failures before the first success, success probability p.
    std::int64_t geometric(double p) noexcept {
        if (p >= 1.0) {
            return 0;
        }
        double u = uniform();
        while (u <= 0.0) {
            u = uniform();
        }
        return static_cast<std::int64_t>(std::floor(std::log(u) / std::log1p(-p)));
    }

private:
    static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept { return (x << k) | (x >> (64 - k)); }

    static std::uint64_t splitmix64(std::uint64_t& x) noexcept {
        std::uint64_t z = (x += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    std::uint64_t s_[4]{};
    double spare_{0.0};
    bool has_spare_{false};
};

}  // namespace mml
