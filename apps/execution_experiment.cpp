// Phase 4 experiment: compare TWAP, VWAP, POV and Almgren-Chriss schedules on
// the same simulated markets (common random numbers across algorithms), after
// calibrating the impact model from the simulator itself.
//
//   mml_execution [--seeds 100] [--horizon 500] [--post 50] [--quantity 600]
//                 [--side buy|sell] [--seasonality 1.5] [--out results]

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cli.hpp"
#include "mml/analytics/stats.hpp"
#include "mml/exec/almgren_chriss.hpp"
#include "mml/exec/algorithms.hpp"
#include "mml/exec/execution_agent.hpp"
#include "mml/sim/impact_model.hpp"
#include "mml/sim/market_simulator.hpp"

using namespace mml;

namespace {

// Risk aversion that produces a given urgency kappa * T (continuous-time
// approximation inverted through the discrete kappa relation).
double lambda_for_urgency(const ImpactParams& ip, double urgency, std::size_t horizon) {
    AlmgrenChrissParams p;
    p.tau = 1.0;
    p.eta = ip.eta;
    p.gamma = ip.gamma;
    const double kappa = urgency / static_cast<double>(horizon);
    const double kappa_tilde_sq = 2.0 * (std::cosh(kappa) - 1.0);
    return kappa_tilde_sq * ac_eta_tilde(p) / (ip.sigma * ip.sigma);
}

struct AlgoSpec {
    std::string label;
    std::function<std::unique_ptr<ExecutionAlgorithm>()> make;
    double urgency{-1.0};  // AC only
    double lambda{0.0};
};

}  // namespace

int main(int argc, char** argv) {
    const cli::Args args(argc, argv);
    const auto seeds = static_cast<std::size_t>(args.get_int("seeds", 100));
    const auto horizon = static_cast<std::size_t>(args.get_int("horizon", 500));
    const auto post = static_cast<std::size_t>(args.get_int("post", 50));
    const Quantity quantity = args.get_int("quantity", 600);
    const Side side = args.get_string("side", "buy") == "sell" ? Side::Sell : Side::Buy;
    const std::string out_dir = args.get_string("out", "results");

    SimConfig base;
    base.steps = horizon + post;
    base.flow.seasonality = args.get_double("seasonality", 1.5);
    base.seed = 1000;

    // ---- 1. Calibrate impact ------------------------------------------------
    std::printf("Calibrating impact model from the simulator...\n");
    SimConfig cal_cfg = base;
    cal_cfg.steps = 4000;
    cal_cfg.flow.seasonality = 0.0;
    const ImpactCalibration cal = calibrate_impact(cal_cfg);
    const ImpactParams& ip = cal.params;
    std::printf("  samples=%zu  sigma=%.4f ticks/sqrt(step)  epsilon=%.4f  eta=%.5f (R^2=%.3f)  gamma=%.5f\n",
                cal.samples.size(), ip.sigma, ip.epsilon, ip.eta, cal.temporary_fit.r2, ip.gamma);
    std::printf("  avg volume/step=%.2f  square-root law Y=%.3f\n\n", cal.avg_volume_per_step, cal.sqrt_law_y);
    {
        auto csv = cli::open_csv(out_dir, "impact_calibration.csv");
        csv << "size,cost_per_unit,permanent\n";
        for (const auto& s : cal.samples) csv << s.size << "," << s.cost_per_unit << "," << s.permanent << "\n";
    }

    // ---- 2. Algorithms --------------------------------------------------------
    MarketSimulator profile_sim(base);
    const std::vector<double> profile = profile_sim.expected_volume_profile(0, horizon);

    std::vector<AlgoSpec> algos;
    algos.push_back({"TWAP", [] { return std::make_unique<Twap>(); }});
    algos.push_back({"VWAP", [profile] { return std::make_unique<Vwap>(profile); }});
    algos.push_back({"POV(20%)", [] { return std::make_unique<Pov>(0.20); }});
    for (double u : {0.5, 2.0, 5.0, 10.0}) {
        const double lambda = lambda_for_urgency(ip, u, horizon);
        AlmgrenChrissParams acp;
        acp.quantity = static_cast<double>(quantity);
        acp.intervals = horizon;
        acp.sigma = ip.sigma;
        acp.epsilon = ip.epsilon;
        acp.eta = ip.eta;
        acp.gamma = ip.gamma;
        acp.lambda = lambda;
        char label[48];
        std::snprintf(label, sizeof(label), "AC(kT=%.1f)", u);
        const std::string name = label;
        algos.push_back({name, [acp, name] { return std::make_unique<AlmgrenChrissAlgo>(acp, name); }, u, lambda});
    }

    // ---- 3. Monte Carlo -----------------------------------------------------------
    std::printf("Running %zu seeds x %zu algorithms (%s %lld over %zu steps)...\n\n", seeds, algos.size(),
                side == Side::Buy ? "buy" : "sell", static_cast<long long>(quantity), horizon);
    std::map<std::string, std::vector<ExecutionReport>> reports;
    auto runs_csv = cli::open_csv(out_dir, "execution_runs.csv");
    runs_csv << "seed,algorithm,filled,fill_rate,shortfall_bps,shortfall_ticks,slippage_vs_vwap_bps,execution_cost,"
                "market_impact,permanent_impact,child_orders\n";

    for (std::size_t s = 0; s < seeds; ++s) {
        for (const AlgoSpec& spec : algos) {
            SimConfig cfg = base;
            cfg.seed = base.seed + s;  // same background randomness for every algorithm
            MarketSimulator sim(cfg);
            ExecutionAgent agent(ParentOrder{side, quantity, 0, horizon}, spec.make());
            sim.add_agent(agent);
            sim.run();
            const ExecutionReport r = agent.report();
            reports[spec.label].push_back(r);
            runs_csv << s << "," << spec.label << "," << r.filled << "," << r.fill_rate << "," << r.shortfall_bps
                     << "," << r.shortfall_ticks << "," << r.slippage_vs_vwap_bps << "," << r.execution_cost << ","
                     << r.market_impact << "," << r.permanent_impact << "," << r.child_orders << "\n";
        }
    }

    // ---- 4. Summary table ---------------------------------------------------------
    auto sum_csv = cli::open_csv(out_dir, "execution_summary.csv");
    sum_csv << "algorithm,mean_is_bps,std_is_bps,p95_is_bps,cvar95_is_bps,mean_is_ticks,std_is_ticks,"
               "mean_vwap_slippage_bps,mean_fill_rate,mean_impact_ticks,mean_permanent_ticks\n";
    std::printf("%-12s %9s %8s %8s %8s | %9s %8s | %9s %7s %8s %8s\n", "algorithm", "IS(bps)", "std", "p95",
                "CVaR95", "IS(tick)", "std", "vsVWAP", "fill", "impact", "perm");
    std::printf("%s\n", std::string(112, '-').c_str());
    for (const AlgoSpec& spec : algos) {
        const auto& rs = reports[spec.label];
        std::vector<double> is_bps, is_ticks, vw, fill, imp, perm;
        for (const auto& r : rs) {
            is_bps.push_back(r.shortfall_bps);
            is_ticks.push_back(r.shortfall_ticks);
            vw.push_back(r.slippage_vs_vwap_bps);
            fill.push_back(r.fill_rate);
            imp.push_back(r.market_impact);
            perm.push_back(r.permanent_impact);
        }
        const Summary sb = summarize(is_bps);
        const Summary st = summarize(is_ticks);
        std::printf("%-12s %9.3f %8.3f %8.3f %8.3f | %9.3f %8.3f | %9.3f %7.3f %8.3f %8.3f\n", spec.label.c_str(),
                    sb.mean, sb.stddev, sb.p95, sb.cvar95, st.mean, st.stddev, mean(vw), mean(fill), mean(imp),
                    mean(perm));
        sum_csv << spec.label << "," << sb.mean << "," << sb.stddev << "," << sb.p95 << "," << sb.cvar95 << ","
                << st.mean << "," << st.stddev << "," << mean(vw) << "," << mean(fill) << "," << mean(imp) << ","
                << mean(perm) << "\n";
    }

    // ---- 5. Almgren-Chriss: model vs simulation ------------------------------------------
    std::printf("\nAlmgren-Chriss frontier: model prediction vs simulated (ticks per unit of parent)\n");
    std::printf("%-12s %12s %10s %10s | %10s %10s\n", "schedule", "lambda", "E[model]", "sd[model]", "E[sim]",
                "sd[sim]");
    auto fr_csv = cli::open_csv(out_dir, "ac_frontier.csv");
    fr_csv << "schedule,urgency,lambda,model_mean_ticks,model_sd_ticks,sim_mean_ticks,sim_sd_ticks\n";
    const double x = static_cast<double>(quantity);
    for (const AlgoSpec& spec : algos) {
        if (spec.urgency < 0.0 && spec.label != "TWAP") {
            continue;
        }
        AlmgrenChrissParams acp;
        acp.quantity = x;
        acp.intervals = horizon;
        acp.sigma = ip.sigma;
        acp.epsilon = ip.epsilon;
        acp.eta = ip.eta;
        acp.gamma = ip.gamma;
        acp.lambda = spec.lambda;
        const double e = ac_expected_cost(acp, ac_trade_list(acp)) / x;
        const double sd = std::sqrt(ac_variance(acp, ac_holdings(acp))) / x;
        std::vector<double> is_ticks;
        for (const auto& r : reports[spec.label]) is_ticks.push_back(r.shortfall_ticks);
        std::printf("%-12s %12.3e %10.3f %10.3f | %10.3f %10.3f\n", spec.label.c_str(), spec.lambda, e, sd,
                    mean(is_ticks), stddev(is_ticks));
        fr_csv << spec.label << "," << spec.urgency << "," << spec.lambda << "," << e << "," << sd << ","
               << mean(is_ticks) << "," << stddev(is_ticks) << "\n";
    }
    std::printf("\nCSV written to %s/\n", out_dir.c_str());
    return 0;
}
