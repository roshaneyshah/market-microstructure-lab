#pragma once

#include <memory>
#include <string>
#include <vector>

#include "mml/core/types.hpp"
#include "mml/exec/almgren_chriss.hpp"

namespace mml {

// What an execution algorithm sees when deciding the next child order.
struct ExecContext {
    std::size_t step{0};      // 0-based step within the execution horizon
    std::size_t horizon{0};   // total steps in the horizon
    Quantity target{0};       // parent order size
    Quantity executed{0};     // filled so far
    Quantity last_market_volume{0};  // volume traded by others in the previous step
    double mid{0.0};
};

// Decides child order sizes for a parent order. Child orders are executed as
// market (immediate) orders by ExecutionAgent. Algorithms are expressed as
// "target cumulative fill by end of step" so they self-correct when a child
// order is only partially filled.
class ExecutionAlgorithm {
public:
    virtual ~ExecutionAlgorithm() = default;
    [[nodiscard]] virtual std::string name() const = 0;
    virtual void on_start(Quantity /*target*/, std::size_t /*horizon*/) {}
    [[nodiscard]] virtual Quantity child_quantity(const ExecContext& ctx) = 0;
};

// Base for algorithms defined by a precomputed cumulative schedule.
class ScheduleAlgorithm : public ExecutionAlgorithm {
public:
    void on_start(Quantity target, std::size_t horizon) override;
    [[nodiscard]] Quantity child_quantity(const ExecContext& ctx) override;
    [[nodiscard]] const std::vector<Quantity>& schedule() const noexcept { return cumulative_; }

protected:
    // Fraction of the parent that should be complete by the end of each step
    // (non-decreasing, last element 1).
    [[nodiscard]] virtual std::vector<double> cumulative_fractions(std::size_t horizon) const = 0;

private:
    std::vector<Quantity> cumulative_;
};

// Time-weighted: equal slices per step.
class Twap final : public ScheduleAlgorithm {
public:
    [[nodiscard]] std::string name() const override { return "TWAP"; }

protected:
    [[nodiscard]] std::vector<double> cumulative_fractions(std::size_t horizon) const override;
};

// Volume-weighted: slices proportional to an expected volume profile.
class Vwap final : public ScheduleAlgorithm {
public:
    explicit Vwap(std::vector<double> volume_profile) : profile_(std::move(volume_profile)) {}
    [[nodiscard]] std::string name() const override { return "VWAP"; }

protected:
    [[nodiscard]] std::vector<double> cumulative_fractions(std::size_t horizon) const override;

private:
    std::vector<double> profile_;
};

// Almgren-Chriss mean-variance optimal trajectory.
class AlmgrenChrissAlgo final : public ScheduleAlgorithm {
public:
    AlmgrenChrissAlgo(AlmgrenChrissParams params, std::string label = {})
        : params_(params), label_(std::move(label)) {}
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] const AlmgrenChrissParams& params() const noexcept { return params_; }

protected:
    [[nodiscard]] std::vector<double> cumulative_fractions(std::size_t horizon) const override;

private:
    AlmgrenChrissParams params_;
    std::string label_;
};

// Percentage of volume: trade a fixed fraction of the market's own volume,
// observed with a one-step lag. Optionally completes any remainder in the
// final step (otherwise fills may fall short of the target).
class Pov final : public ExecutionAlgorithm {
public:
    explicit Pov(double participation, bool complete_at_end = false)
        : rate_(participation), complete_(complete_at_end) {}
    [[nodiscard]] std::string name() const override;
    [[nodiscard]] Quantity child_quantity(const ExecContext& ctx) override;

private:
    double rate_;
    bool complete_;
    double carry_{0.0};  // fractional units carried between steps
};

}  // namespace mml
