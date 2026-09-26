// Phase 5 experiment: quoting strategies on the simulated market with maker
// rebates and taker fees. Reports PnL attribution, inventory risk, markouts
// (adverse selection) overall and by volatility regime, and the predictive
// power of the queue-imbalance signal.
//
//   mml_market_making [--seeds 20] [--steps 5000] [--maker-fee -0.1]
//                     [--taker-fee 0.3] [--out results]

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "cli.hpp"
#include "mml/analytics/stats.hpp"
#include "mml/mm/market_maker.hpp"
#include "mml/mm/markout.hpp"
#include "mml/sim/market_simulator.hpp"

using namespace mml;

namespace {

struct StrategySpec {
    std::string name;
    std::function<std::unique_ptr<MarketMaker>(const FeeSchedule&)> make;
};

struct RunResult {
    double total{0.0};
    PnlAttribution attribution{};
    TradingStats stats{};
    double mean_abs_inventory{0.0};
    double step_sharpe{0.0};
    Quantity final_inventory{0};
    std::vector<FillRecord> fills;
    std::vector<int> fill_regimes;
    std::vector<StepRecord> history;
};

RunResult run_one(const StrategySpec& spec, const SimConfig& cfg, const FeeSchedule& fees) {
    MarketSimulator sim(cfg);
    auto mm = spec.make(fees);
    sim.add_agent(*mm);
    sim.run();

    RunResult r;
    const Portfolio& p = mm->portfolio();
    r.total = p.total_pnl();
    r.attribution = p.attribution();
    r.stats = p.stats();
    r.final_inventory = p.position();
    double abs_inv = 0.0;
    for (Quantity q : mm->inventory_path()) abs_inv += std::fabs(static_cast<double>(q));
    r.mean_abs_inventory = abs_inv / static_cast<double>(std::max<std::size_t>(mm->inventory_path().size(), 1));
    std::vector<double> dpnl;
    const auto& path = mm->pnl_path();
    for (std::size_t i = 1; i < path.size(); ++i) dpnl.push_back(path[i] - path[i - 1]);
    const double sd = stddev(dpnl);
    r.step_sharpe = sd > 0.0 ? mean(dpnl) / sd : 0.0;
    r.fills = mm->fills();
    r.history = sim.history();
    for (const auto& f : r.fills) r.fill_regimes.push_back(f.step < r.history.size() ? r.history[f.step].regime : 0);
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const cli::Args args(argc, argv);
    const auto seeds = static_cast<std::size_t>(args.get_int("seeds", 20));
    const std::string out_dir = args.get_string("out", "results");
    const FeeSchedule fees{args.get_double("maker-fee", -0.1), args.get_double("taker-fee", 0.3)};

    SimConfig base;
    base.steps = static_cast<std::size_t>(args.get_int("steps", 5000));
    base.seed = 5000;

    std::vector<StrategySpec> strategies;
    strategies.push_back({"Symmetric(join)", [](const FeeSchedule& f) {
                              return std::make_unique<SymmetricMarketMaker>(SymmetricParams{0.5, 5}, f,
                                                                            "Symmetric(join)");
                          }});
    strategies.push_back({"Symmetric(wide)", [](const FeeSchedule& f) {
                              return std::make_unique<SymmetricMarketMaker>(SymmetricParams{1.5, 5}, f,
                                                                            "Symmetric(wide)");
                          }});
    strategies.push_back({"InventorySkew", [](const FeeSchedule& f) {
                              return std::make_unique<InventoryAwareMarketMaker>(InventoryParams{0.5, 5, 0.05, 60}, f,
                                                                                 "InventorySkew");
                          }});
    strategies.push_back({"AS", [](const FeeSchedule& f) {
                              return std::make_unique<AvellanedaStoikovMarketMaker>(AvellanedaStoikovParams{}, f,
                                                                                    "AS");
                          }});
    strategies.push_back({"AS+imbalance", [](const FeeSchedule& f) {
                              AvellanedaStoikovParams p;
                              p.alpha_imbalance = 1.0;
                              return std::make_unique<AvellanedaStoikovMarketMaker>(p, f, "AS+imbalance");
                          }});

    std::printf("Market making: %zu seeds x %zu steps, maker fee %.2f, taker fee %.2f (ticks/unit)\n\n", seeds,
                base.steps, fees.maker_fee, fees.taker_fee);

    std::map<std::string, std::vector<RunResult>> results;
    auto runs_csv = cli::open_csv(out_dir, "mm_runs.csv");
    runs_csv << "seed,strategy,total_pnl,spread_capture,inventory_pnl,fees,fills,volume,max_abs_inventory,"
                "mean_abs_inventory,final_inventory,step_sharpe\n";
    for (std::size_t s = 0; s < seeds; ++s) {
        for (const auto& spec : strategies) {
            SimConfig cfg = base;
            cfg.seed = base.seed + s;
            RunResult r = run_one(spec, cfg, fees);
            runs_csv << s << "," << spec.name << "," << r.total << "," << r.attribution.spread_capture << ","
                     << r.attribution.inventory << "," << r.attribution.fees << "," << r.stats.fills << ","
                     << r.stats.volume << "," << r.stats.max_abs_position << "," << r.mean_abs_inventory << ","
                     << r.final_inventory << "," << r.step_sharpe << "\n";
            results[spec.name].push_back(std::move(r));
        }
    }

    // ---- PnL attribution ------------------------------------------------------------------
    std::printf("PnL attribution (ticks, mean per run)\n");
    std::printf("%-16s %9s %9s %9s %9s %9s | %7s %8s %7s %7s %8s\n", "strategy", "total", "sd", "spread", "invent.",
                "fees", "fills", "volume", "max|q|", "avg|q|", "win%");
    std::printf("%s\n", std::string(114, '-').c_str());
    auto sum_csv = cli::open_csv(out_dir, "mm_summary.csv");
    sum_csv << "strategy,mean_pnl,sd_pnl,spread_capture,inventory_pnl,fees,fills,volume,max_abs_inventory,"
               "mean_abs_inventory,win_rate,mean_step_sharpe\n";
    for (const auto& spec : strategies) {
        const auto& rs = results[spec.name];
        std::vector<double> tot, sc, inv, fee, fills, vol, maxq, avgq, sharpe;
        double wins = 0.0;
        for (const auto& r : rs) {
            tot.push_back(r.total);
            sc.push_back(r.attribution.spread_capture);
            inv.push_back(r.attribution.inventory);
            fee.push_back(r.attribution.fees);
            fills.push_back(static_cast<double>(r.stats.fills));
            vol.push_back(static_cast<double>(r.stats.volume));
            maxq.push_back(static_cast<double>(r.stats.max_abs_position));
            avgq.push_back(r.mean_abs_inventory);
            sharpe.push_back(r.step_sharpe);
            wins += r.total > 0.0 ? 1.0 : 0.0;
        }
        const double win = 100.0 * wins / static_cast<double>(rs.size());
        std::printf("%-16s %9.1f %9.1f %9.1f %9.1f %9.1f | %7.0f %8.0f %7.1f %7.1f %7.0f%%\n", spec.name.c_str(),
                    mean(tot), stddev(tot), mean(sc), mean(inv), mean(fee), mean(fills), mean(vol), mean(maxq),
                    mean(avgq), win);
        sum_csv << spec.name << "," << mean(tot) << "," << stddev(tot) << "," << mean(sc) << "," << mean(inv) << ","
                << mean(fee) << "," << mean(fills) << "," << mean(vol) << "," << mean(maxq) << "," << mean(avgq)
                << "," << win << "," << mean(sharpe) << "\n";
    }
    std::printf("(fees column is fees paid; negative means net rebates earned)\n");

    // ---- Markouts --------------------------------------------------------------------------
    const std::vector<std::size_t> horizons{0, 1, 5, 20, 50};
    std::printf("\nMarkouts per unit (ticks): spread capture + adverse move = markout\n");
    std::printf("%-16s %6s", "strategy", "capt.");
    for (auto h : horizons) std::printf("   adv@%-3zu", h);
    for (auto h : horizons) std::printf("   mkt@%-3zu", h);
    std::printf("\n");
    auto mk_csv = cli::open_csv(out_dir, "mm_markouts.csv");
    mk_csv << "strategy,regime,horizon,fills,spread_capture,adverse_move,markout\n";
    for (const auto& spec : strategies) {
        // Pool fills across seeds, weighting by quantity.
        std::vector<double> capt_h(horizons.size()), adv_h(horizons.size()), qty_h(horizons.size());
        std::map<int, std::vector<double>> reg_adv, reg_qty, reg_capt;
        std::map<int, std::vector<std::size_t>> reg_n;
        for (const auto& r : results[spec.name]) {
            const auto m = compute_markouts(r.fills, r.history, horizons);
            for (std::size_t i = 0; i < horizons.size(); ++i) {
                double q = 0.0;
                for (const auto& f : r.fills) {
                    if (f.step + horizons[i] < r.history.size()) q += static_cast<double>(f.quantity);
                }
                capt_h[i] += m[i].spread_capture * q;
                adv_h[i] += m[i].adverse_move * q;
                qty_h[i] += q;
            }
            for (int regime : {0, 1}) {
                std::vector<FillRecord> sub;
                for (std::size_t k = 0; k < r.fills.size(); ++k) {
                    if (r.fill_regimes[k] == regime) sub.push_back(r.fills[k]);
                }
                const auto mr = compute_markouts(sub, r.history, horizons);
                auto& adv = reg_adv[regime];
                auto& qty = reg_qty[regime];
                auto& capt = reg_capt[regime];
                auto& n = reg_n[regime];
                adv.resize(horizons.size());
                qty.resize(horizons.size());
                capt.resize(horizons.size());
                n.resize(horizons.size());
                for (std::size_t i = 0; i < horizons.size(); ++i) {
                    double q = 0.0;
                    for (const auto& f : sub) {
                        if (f.step + horizons[i] < r.history.size()) q += static_cast<double>(f.quantity);
                    }
                    adv[i] += mr[i].adverse_move * q;
                    capt[i] += mr[i].spread_capture * q;
                    qty[i] += q;
                    n[i] += mr[i].fills;
                }
            }
        }
        std::printf("%-16s %6.3f", spec.name.c_str(), qty_h[0] > 0 ? capt_h[0] / qty_h[0] : 0.0);
        for (std::size_t i = 0; i < horizons.size(); ++i) std::printf(" %9.3f", qty_h[i] > 0 ? adv_h[i] / qty_h[i] : 0.0);
        for (std::size_t i = 0; i < horizons.size(); ++i) {
            std::printf(" %9.3f", qty_h[i] > 0 ? (capt_h[i] + adv_h[i]) / qty_h[i] : 0.0);
        }
        std::printf("\n");
        for (int regime : {0, 1}) {
            for (std::size_t i = 0; i < horizons.size(); ++i) {
                const double q = reg_qty[regime][i];
                const double c = q > 0 ? reg_capt[regime][i] / q : 0.0;
                const double a = q > 0 ? reg_adv[regime][i] / q : 0.0;
                mk_csv << spec.name << "," << (regime == 0 ? "calm" : "volatile") << "," << horizons[i] << ","
                       << reg_n[regime][i] << "," << c << "," << a << "," << c + a << "\n";
            }
        }
        const double qc = reg_qty[0][3], qv = reg_qty[1][3];
        std::printf("%-16s adverse@20 by regime: calm %.3f (%.0f units)  volatile %.3f (%.0f units)\n", "",
                    qc > 0 ? reg_adv[0][3] / qc : 0.0, qc, qv > 0 ? reg_adv[1][3] / qv : 0.0, qv);
    }

    // ---- Signal study -----------------------------------------------------------------------
    // Uses background-only markets so the signal is not contaminated by a strategy's quotes.
    std::printf("\nQueue-imbalance signal: corr(I_t, mid_{t+h} - mid_t) on background-only markets\n");
    auto sig_csv = cli::open_csv(out_dir, "signal_study.csv");
    sig_csv << "horizon,correlation,slope_ticks_per_unit_imbalance,r2,n\n";
    for (std::size_t h : {1u, 5u, 20u}) {
        std::vector<double> xs, ys;
        for (std::size_t s = 0; s < std::min<std::size_t>(seeds, 10); ++s) {
            SimConfig cfg = base;
            cfg.seed = base.seed + s;
            MarketSimulator sim(cfg);
            sim.run();
            const auto& hist = sim.history();
            for (std::size_t t = 0; t + h < hist.size(); ++t) {
                const double tot = static_cast<double>(hist[t].bid_top + hist[t].ask_top);
                if (tot <= 0.0) continue;
                xs.push_back(static_cast<double>(hist[t].bid_top - hist[t].ask_top) / tot);
                ys.push_back(hist[t + h].mid - hist[t].mid);
            }
        }
        const OlsFit f = ols(xs, ys);
        std::printf("  h=%-3zu corr=%.3f  slope=%.3f ticks  R^2=%.3f  n=%zu\n", h, correlation(xs, ys), f.slope, f.r2,
                    f.n);
        sig_csv << h << "," << correlation(xs, ys) << "," << f.slope << "," << f.r2 << "," << f.n << "\n";
    }
    std::printf("\nCSV written to %s/\n", out_dir.c_str());
    return 0;
}
