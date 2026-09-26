# Market Microstructure Lab

A high-performance C++20 market microstructure and electronic exchange simulator for exploring limit order books, order matching, execution algorithms, and quantitative trading strategies.

The project is built from the ground up with an emphasis on correctness, performance, realistic exchange mechanics, and reproducible quantitative experiments.

[![CI](https://github.com/roshaneyshah/market-microstructure-lab/actions/workflows/ci.yml/badge.svg)](https://github.com/roshaneyshah/market-microstructure-lab/actions/workflows/ci.yml)

## Overview

Modern electronic markets operate through limit order books where participants continuously submit, cancel, and execute orders. Market Microstructure Lab recreates the core mechanics of this process in C++ and provides an environment for studying questions such as:

- How should orders be efficiently represented and matched?
- How does price-time priority affect execution?
- How do liquidity and order flow affect execution quality?
- What is the cost of executing a large order?
- How do market makers balance spread capture against inventory risk?
- How do latency and data-structure choices affect exchange performance?

This is a research and experimentation platform, not a production trading system.

## Highlights

- **Exchange core.** Price-time priority matching with limit, market, IOC and FOK orders, partial fills, cancel, and priority-aware modify. Four interchangeable book implementations, verified against a naive reference book by differential fuzzing.
- **Performance.** Zero heap allocation per message in steady state for the fastest variant, about 55 ns per message (18M messages/s) on a 10k-order book, with latency percentiles, allocation counting and cache-scaling analysis.
- **Market simulation.** Regime-switching fundamental, partially informed takers, Poisson limit/market/cancel flow, intraday seasonality, and an impact model calibrated from the simulator itself.
- **Execution.** TWAP, VWAP, POV and Almgren-Chriss compared on common random numbers, with implementation shortfall, VWAP slippage, fill rate, impact and tail risk (CVaR).
- **Market making.** Symmetric, inventory-skew and Avellaneda-Stoikov makers, order-flow imbalance signals, maker rebates and taker fees, exact PnL attribution, and markout-based adverse-selection analysis by volatility regime.
- **Reproducible.** Deterministic cross-platform RNG, no external dependencies, CI with GCC, Clang and sanitizers.

## Architecture

```
                         ┌─────────────────┐
                         │ Market Simulator│
                         └────────┬────────┘
                                  │
                                  ▼
┌──────────────┐        ┌─────────────────┐
│   Strategy   │───────▶│  Order Gateway  │
└──────────────┘        └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Matching Engine │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Limit Order Book│
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Trades / Fills  │
                         └────────┬────────┘
                                  │
                                  ▼
                         ┌─────────────────┐
                         │ Portfolio & PnL │
                         └─────────────────┘
```

Details: [docs/architecture.md](docs/architecture.md).

## Quick start

Requirements: a C++20 compiler (GCC 11+, Clang 14+ or MSVC 19.30+) and CMake 3.20+. There are no other dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

./build/mml_demo                 # walkthrough of the matching engine
./build/mml_bench                # Phase 2 benchmarks
./build/mml_execution            # Phase 4 execution study
./build/mml_market_making        # Phase 5 market-making study
```

Build options: `-DMML_SANITIZE=ON` (ASan + UBSan), `-DMML_NATIVE=ON` (`-march=native`), `-DMML_BUILD_TESTS|BENCHMARKS|APPS=OFF`.

### Using the library

```cpp
#include "mml/engine/matching_engine.hpp"
#include "mml/engine/order_gateway.hpp"

mml::MatchingEngine engine;
mml::OrderGateway gateway(engine);
const auto trader = gateway.register_trader();

gateway.limit(trader, mml::Side::Sell, 10'001, 50);
gateway.limit(trader, mml::Side::Buy, 9'999, 50);
auto r = gateway.market(trader, mml::Side::Buy, 20);   // r.result.filled == 20

const auto& book = engine.book();
auto spread = book.spread();                            // 2 ticks
auto top5 = book.depth(mml::Side::Sell, 5);
```

## Results at a glance

Figures below come from the committed runs in [`results/`](results/) (Intel Xeon 2.1 GHz VM, GCC 13, Release).

**Order book variants** (10k resting orders, 1M messages):

| Variant | ns/msg | allocs/msg | cancel p50 (ns) |
|---|---:|---:|---:|
| `std::map` levels + hash index | 116.4 | 0.66 | 118 |
| flat ladder + dense index | 55.4 | 0.00 | 90 |

**Execution** (buy 600 units over 500 steps, 500 seeds):

| Algorithm | IS mean (bps) | IS sd (bps) | CVaR95 (bps) |
|---|---:|---:|---:|
| TWAP | 3.92 | 7.36 | 20.16 |
| VWAP | 3.53 | 6.01 | 15.51 |
| Almgren-Chriss (kT=10) | 4.05 | 2.76 | 9.86 |

**Market making** (20 seeds x 5,000 steps, PnL in ticks):

| Strategy | Mean PnL | PnL sd | Avg \|inventory\| |
|---|---:|---:|---:|
| Symmetric (join touch) | 1,172 | 4,950 | 78.5 |
| Inventory skew | 2,430 | 374 | 3.5 |
| Avellaneda-Stoikov | 1,259 | 139 | 1.3 |

## Roadmap

### Phase 1: Exchange Core ([docs](docs/exchange-core.md))
- [x] Order representation
- [x] Bid and ask books
- [x] Limit orders
- [x] Market orders
- [x] Price-time priority
- [x] Partial fills
- [x] Order cancellation
- [x] Order modification
- [x] Trade generation
- [x] Unit tests (plus differential fuzzing against a reference book)

### Phase 2: Performance ([docs](docs/performance.md))
- [x] C++ benchmarking suite
- [x] Matching latency measurements
- [x] Throughput measurements
- [x] Efficient order-ID lookup
- [x] Memory allocation analysis
- [x] Alternative order-book data structures
- [x] Cache-performance investigation

### Phase 3: Market Simulation ([docs](docs/simulation.md))
- [x] Synthetic order-flow generator
- [x] Configurable spread and liquidity
- [x] Stochastic market-order arrivals
- [x] Limit-order arrivals and cancellations
- [x] Volatility regimes
- [x] Market-impact model

### Phase 4: Quantitative Execution ([docs](docs/execution.md))
- [x] TWAP
- [x] VWAP
- [x] Percentage of Volume (POV)
- [x] Almgren-Chriss optimal execution
- [x] Evaluation: implementation shortfall, slippage, fill rate, execution cost, market impact, execution-risk distribution

### Phase 5: Market Making ([docs](docs/market-making.md))
- [x] Baseline symmetric market maker
- [x] Inventory tracking
- [x] Inventory-aware quoting
- [x] Avellaneda-Stoikov-inspired strategy
- [x] Order-flow imbalance signals
- [x] Adverse-selection analysis
- [x] Transaction fees and rebates
- [x] Strategy P&L attribution

### Future work
- [ ] Hawkes and queue-reactive order flow
- [ ] Agent latency and message delays
- [ ] Replay of historical L3 data (for example ITCH)
- [ ] Self-trade prevention and additional order types (stop, iceberg, post-only flag in the book)
- [ ] Python bindings for analysis notebooks

## Repository layout

```
include/mml/
  core/        types, orders, price levels, id indexes, object pool, BasicOrderBook
  engine/      MatchingEngine, OrderGateway
  portfolio/   Portfolio, fees, PnL attribution
  sim/         RNG, background order flow, MarketSimulator, impact model
  exec/        Almgren-Chriss, TWAP/VWAP/POV/AC algorithms, ExecutionAgent
  mm/          MarketMaker strategies, order-flow signals, markouts
  analytics/   statistics and regressions
src/           library implementation
apps/          demo and experiment executables
bench/         benchmark suite
tests/         unit, integration and fuzz tests (self-contained harness)
docs/          design notes and experiment write-ups
results/       committed outputs of the experiments
```

## Technical goals

- Modern C++20, warnings-clean under `-Wall -Wextra -Wpedantic -Wshadow`.
- Integer tick prices in the core; floating point only for analytics.
- Deterministic, seed-reproducible simulations on every platform.
- No dependencies beyond the standard library.
- Every claim in the docs backed by a committed, reproducible run.

## References

- Almgren, R. and Chriss, N. (2000). Optimal execution of portfolio transactions. *Journal of Risk*, 3(2).
- Avellaneda, M. and Stoikov, S. (2008). High-frequency trading in a limit order book. *Quantitative Finance*, 8(3).
- Cont, R., Kukanov, A. and Stoikov, S. (2014). The price impact of order book events. *Journal of Financial Econometrics*, 12(1).
- Kyle, A. S. (1985). Continuous auctions and insider trading. *Econometrica*, 53(6).
- Bouchaud, J.-P., Bonart, J., Donier, J. and Gould, M. (2018). *Trades, Quotes and Prices*. Cambridge University Press.
- Cartea, A., Jaimungal, S. and Penalva, J. (2015). *Algorithmic and High-Frequency Trading*. Cambridge University Press.

## License

MIT. See [LICENSE](LICENSE).
