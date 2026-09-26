// Phase 2 benchmark suite.
//
// A synthetic but realistic message stream (passive adds near the touch,
// cancels of live orders, small aggressive orders) is generated once and then
// replayed into each order book variant:
//
//   map+hash      std::map levels,  std::unordered_map id index
//   map+dense     std::map levels,  direct-mapped id vector
//   ladder+hash   flat price array, std::unordered_map id index
//   ladder+dense  flat price array, direct-mapped id vector
//
// Reported per variant: throughput, per-message latency percentiles by
// message type, heap allocations per message (global operator new is
// instrumented), and how cost scales with the number of resting orders.
//
//   mml_bench [--ops 1000000] [--resting 10000] [--quick] [--out results]

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "../apps/cli.hpp"
#include "mml/core/order_book.hpp"
#include "mml/sim/random.hpp"

// ---- Allocation instrumentation --------------------------------------------------
namespace {
std::uint64_t g_alloc_count = 0;
std::uint64_t g_alloc_bytes = 0;
}  // namespace

void* operator new(std::size_t n) {
    ++g_alloc_count;
    g_alloc_bytes += n;
    if (void* p = std::malloc(n == 0 ? 1 : n)) {
        return p;
    }
    throw std::bad_alloc();
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

using namespace mml;

namespace {

enum class OpKind : std::uint8_t { Add, Cancel, Aggress };

struct Op {
    OpKind kind;
    OrderRequest req;  // for Cancel only req.id is used
};

struct Workload {
    std::vector<Op> prefill;
    std::vector<Op> ops;
};

constexpr Price kMid = 50'000;

// Generate the stream by driving a reference book so that cancels always
// target orders that are actually live, and the resting population stays
// close to `resting`.
Workload make_workload(std::size_t resting, std::size_t n_ops, std::uint64_t seed) {
    Workload w;
    Rng rng(seed);
    MapDenseOrderBook book;
    std::vector<Trade> trades;
    std::vector<OrderId> live;
    OrderId next = 1;
    // Wider books for more resting orders so level counts grow too.
    const double depth_p = std::clamp(40.0 / static_cast<double>(resting), 0.0005, 0.3);

    auto passive = [&](Side side) {
        const Price off = 1 + rng.geometric(depth_p);
        const Price px = side == Side::Buy ? kMid - off : kMid + off;
        return OrderRequest::limit(next++, side, std::clamp<Price>(px, 2, 99'999), rng.uniform_int(1, 100));
    };

    for (std::size_t i = 0; i < resting; ++i) {
        const OrderRequest r = passive(i % 2 == 0 ? Side::Buy : Side::Sell);
        book.add(r, trades);
        live.push_back(r.id);
        w.prefill.push_back({OpKind::Add, r});
    }
    trades.clear();

    w.ops.reserve(n_ops);
    while (w.ops.size() < n_ops) {
        const double u = rng.uniform();
        const double fill_bias = live.size() < resting ? 0.08 : -0.08;
        if (u < 0.45 + fill_bias || live.empty()) {
            const OrderRequest r = passive(rng.bernoulli(0.5) ? Side::Buy : Side::Sell);
            book.add(r, trades);
            live.push_back(r.id);
            w.ops.push_back({OpKind::Add, r});
        } else if (u < 0.92) {
            const auto i = static_cast<std::size_t>(rng.uniform_int(0, static_cast<std::int64_t>(live.size()) - 1));
            const OrderId id = live[i];
            live[i] = live.back();
            live.pop_back();
            if (book.cancel(id)) {  // skip ids that were already filled
                OrderRequest r;
                r.id = id;
                w.ops.push_back({OpKind::Cancel, r});
            }
        } else {
            const Side side = rng.bernoulli(0.5) ? Side::Buy : Side::Sell;
            const OrderRequest r = OrderRequest::market(next++, side, rng.uniform_int(1, 150));
            book.add(r, trades);
            w.ops.push_back({OpKind::Aggress, r});
        }
        trades.clear();
    }
    return w;
}

struct Percentiles {
    double p50{0}, p90{0}, p99{0}, p999{0}, max{0};
    std::size_t n{0};
};

Percentiles percentiles(std::vector<std::uint32_t>& v) {
    Percentiles p;
    p.n = v.size();
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return static_cast<double>(v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))]); };
    p.p50 = at(0.50);
    p.p90 = at(0.90);
    p.p99 = at(0.99);
    p.p999 = at(0.999);
    p.max = static_cast<double>(v.back());
    return p;
}

struct Result {
    std::string name;
    double ns_per_op{0};
    double mops{0};
    double allocs_per_op{0};
    double bytes_per_op{0};
    Percentiles add, cancel, aggress;
    std::uint64_t checksum{0};
};

template <class Book>
std::uint64_t apply(Book& book, const Op& op, std::vector<Trade>& trades) {
    trades.clear();
    if (op.kind == OpKind::Cancel) {
        return book.cancel(op.req.id) ? 1u : 0u;
    }
    return static_cast<std::uint64_t>(book.add(op.req, trades).filled);
}

using Clock = std::chrono::steady_clock;

template <class Book>
Result run_variant(const char* name, const Workload& w, bool measure_latency) {
    Result res;
    res.name = name;
    BookConfig cfg;
    cfg.expected_orders = w.prefill.size() * 2 + 1024;

    // Throughput pass.
    {
        Book book(cfg);
        std::vector<Trade> trades;
        trades.reserve(1024);
        for (const Op& op : w.prefill) apply(book, op, trades);
        const std::uint64_t a0 = g_alloc_count, b0 = g_alloc_bytes;
        const auto t0 = Clock::now();
        std::uint64_t sum = 0;
        for (const Op& op : w.ops) sum += apply(book, op, trades);
        const auto t1 = Clock::now();
        const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
        res.ns_per_op = ns / static_cast<double>(w.ops.size());
        res.mops = 1e3 / res.ns_per_op;
        res.allocs_per_op = static_cast<double>(g_alloc_count - a0) / static_cast<double>(w.ops.size());
        res.bytes_per_op = static_cast<double>(g_alloc_bytes - b0) / static_cast<double>(w.ops.size());
        res.checksum = sum;
    }
    if (!measure_latency) return res;

    // Latency pass: time each message individually.
    Book book(cfg);
    std::vector<Trade> trades;
    trades.reserve(1024);
    for (const Op& op : w.prefill) apply(book, op, trades);
    std::vector<std::uint32_t> add, cancel, aggress;
    add.reserve(w.ops.size());
    cancel.reserve(w.ops.size());
    aggress.reserve(w.ops.size() / 4);
    std::uint64_t sum = 0;
    for (const Op& op : w.ops) {
        const auto t0 = Clock::now();
        sum += apply(book, op, trades);
        const auto t1 = Clock::now();
        const auto ns = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        (op.kind == OpKind::Add ? add : op.kind == OpKind::Cancel ? cancel : aggress).push_back(ns);
    }
    if (sum != res.checksum) std::fprintf(stderr, "checksum mismatch in %s\n", name);
    res.add = percentiles(add);
    res.cancel = percentiles(cancel);
    res.aggress = percentiles(aggress);
    return res;
}

double clock_overhead_ns() {
    std::vector<std::uint32_t> v(100000);
    for (auto& x : v) {
        const auto t0 = Clock::now();
        const auto t1 = Clock::now();
        x = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }
    return percentiles(v).p50;
}

}  // namespace

int main(int argc, char** argv) {
    const cli::Args args(argc, argv);
    const bool quick = args.has("quick");
    const auto n_ops = static_cast<std::size_t>(args.get_int("ops", quick ? 200'000 : 1'000'000));
    const auto resting = static_cast<std::size_t>(args.get_int("resting", 10'000));
    const std::string out_dir = args.get_string("out", "results");

    std::printf("Order book benchmark: %zu messages, ~%zu resting orders\n", n_ops, resting);
    std::printf("clock overhead (p50 of back-to-back now()): %.0f ns, included in latency figures\n\n",
                clock_overhead_ns());

    const Workload w = make_workload(resting, n_ops, 42);
    std::size_t n_add = 0, n_cancel = 0, n_aggr = 0;
    for (const Op& op : w.ops) {
        (op.kind == OpKind::Add ? n_add : op.kind == OpKind::Cancel ? n_cancel : n_aggr)++;
    }
    std::printf("mix: %.1f%% add, %.1f%% cancel, %.1f%% aggressive\n\n", 100.0 * n_add / w.ops.size(),
                100.0 * n_cancel / w.ops.size(), 100.0 * n_aggr / w.ops.size());

    std::vector<Result> results;
    results.push_back(run_variant<OrderBook>("map+hash", w, true));
    results.push_back(run_variant<MapDenseOrderBook>("map+dense", w, true));
    results.push_back(run_variant<LadderHashOrderBook>("ladder+hash", w, true));
    results.push_back(run_variant<LadderOrderBook>("ladder+dense", w, true));

    std::printf("%-13s %9s %8s %10s %10s\n", "variant", "ns/msg", "Mmsg/s", "allocs/msg", "bytes/msg");
    for (const auto& r : results) {
        std::printf("%-13s %9.1f %8.2f %10.3f %10.1f\n", r.name.c_str(), r.ns_per_op, r.mops, r.allocs_per_op,
                    r.bytes_per_op);
    }
    std::printf("\nLatency per message type (ns): p50 / p99 / p99.9\n");
    std::printf("%-13s %22s %22s %22s\n", "variant", "add", "cancel", "aggressive");
    for (const auto& r : results) {
        std::printf("%-13s %6.0f /%6.0f /%7.0f %6.0f /%6.0f /%7.0f %6.0f /%6.0f /%7.0f\n", r.name.c_str(), r.add.p50,
                    r.add.p99, r.add.p999, r.cancel.p50, r.cancel.p99, r.cancel.p999, r.aggress.p50, r.aggress.p99,
                    r.aggress.p999);
    }
    for (const auto& r : results) {
        if (r.checksum != results.front().checksum) {
            std::printf("\nERROR: variants disagree on fills (checksum)\n");
            return 1;
        }
    }

    auto csv = cli::open_csv(out_dir, "bench_variants.csv");
    csv << "variant,ns_per_msg,mmsg_per_s,allocs_per_msg,bytes_per_msg,add_p50,add_p99,add_p999,cancel_p50,"
           "cancel_p99,cancel_p999,aggr_p50,aggr_p99,aggr_p999\n";
    for (const auto& r : results) {
        csv << r.name << "," << r.ns_per_op << "," << r.mops << "," << r.allocs_per_op << "," << r.bytes_per_op << ","
            << r.add.p50 << "," << r.add.p99 << "," << r.add.p999 << "," << r.cancel.p50 << "," << r.cancel.p99
            << "," << r.cancel.p999 << "," << r.aggress.p50 << "," << r.aggress.p99 << "," << r.aggress.p999 << "\n";
    }

    // ---- Scaling with book size (cache behaviour) --------------------------------------
    std::printf("\nScaling with resting orders (ns/msg, throughput pass)\n");
    std::printf("%10s %12s %12s %12s %12s\n", "resting", "map+hash", "map+dense", "ladder+hash", "ladder+dense");
    auto scsv = cli::open_csv(out_dir, "bench_scaling.csv");
    scsv << "resting,map_hash,map_dense,ladder_hash,ladder_dense\n";
    const std::vector<std::size_t> sizes =
        quick ? std::vector<std::size_t>{1'000, 10'000, 100'000} : std::vector<std::size_t>{1'000, 10'000, 100'000, 1'000'000};
    for (std::size_t n : sizes) {
        const Workload ws = make_workload(n, quick ? 100'000 : 400'000, 7);
        const double a = run_variant<OrderBook>("", ws, false).ns_per_op;
        const double b = run_variant<MapDenseOrderBook>("", ws, false).ns_per_op;
        const double c = run_variant<LadderHashOrderBook>("", ws, false).ns_per_op;
        const double d = run_variant<LadderOrderBook>("", ws, false).ns_per_op;
        std::printf("%10zu %12.1f %12.1f %12.1f %12.1f\n", n, a, b, c, d);
        scsv << n << "," << a << "," << b << "," << c << "," << d << "\n";
    }
    std::printf("\nCSV written to %s/\n", out_dir.c_str());
    return 0;
}
