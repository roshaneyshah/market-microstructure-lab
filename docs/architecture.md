# Architecture

```
                         ┌─────────────────┐
                         │ Market Simulator│  background flow, regimes, clock
                         └────────┬────────┘
                                  │
┌──────────────┐                  ▼
│   Strategy   │───────▶┌─────────────────┐
│   (Agent)    │◀───────│  Order Gateway  │  ids, risk checks, ownership, fill routing
└──────┬───────┘  fills └────────┬────────┘
       │                         ▼
       │                ┌─────────────────┐
       │                │ Matching Engine │  sequencing, trade tape, market stats
       │                └────────┬────────┘
       │                         ▼
       │                ┌─────────────────┐
       │                │ Limit Order Book│  price-time priority matching
       │                └────────┬────────┘
       │                         ▼
       │                ┌─────────────────┐
       │                │ Trades / Fills  │
       │                └────────┬────────┘
       ▼                         ▼
┌─────────────────────────────────────────┐
│            Portfolio & PnL              │  position, cash, fees, attribution
└─────────────────────────────────────────┘
```

## Layers

| Layer | Headers | Responsibility |
|---|---|---|
| Core | `include/mml/core/` | Types, order and trade records, `BasicOrderBook` with pluggable level and id-index policies, object pool |
| Engine | `include/mml/engine/` | `MatchingEngine` (clock, tape, stats) and `OrderGateway` (participant API) |
| Portfolio | `include/mml/portfolio/` | Position, cash, average cost, fees, exact PnL attribution |
| Simulation | `include/mml/sim/` | Deterministic RNG, background order flow, regime switching, impact model and calibration, `MarketSimulator` and `Agent` |
| Execution | `include/mml/exec/` | Almgren-Chriss math, TWAP/VWAP/POV/AC algorithms, `ExecutionAgent`, execution metrics |
| Market making | `include/mml/mm/` | Quoting base class, symmetric, inventory-skew and Avellaneda-Stoikov makers, order-flow signals, markouts |
| Analytics | `include/mml/analytics/` | Summary statistics, tail risk, regressions |

The core is header-only (it is a template). Everything above it compiles into the static library `mml`.

## Design choices

**Integer ticks everywhere in the core.** Prices are `int64` ticks and quantities are `int64` units. Matching never compares floating point values. Conversion to currency is a presentation concern.

**Single-threaded, deterministic.** The book, engine and simulator are single-threaded by design. Every run is exactly reproducible from its seed, including across compilers, because the RNG and all distributions are implemented in `random.hpp` instead of relying on implementation-defined `std::` distributions. A real exchange would scale by sharding instruments, not by locking one book.

**Policy-based book.** `BasicOrderBook<Levels, Index>` separates the matching logic from two storage decisions, so the benchmark suite can compare them with identical semantics:

- `MapLevels` (red-black tree) vs `LadderLevels` (flat array over a tick range)
- `HashIndex` (`std::unordered_map`) vs `DenseIndex` (direct-mapped vector)

**Intrusive FIFO queues and pooled nodes.** Each price level is a doubly linked list of pooled `OrderNode`s. Cancel is O(1) after the id lookup, and nodes are recycled through `ObjectPool`, so steady-state order flow does not touch the allocator for nodes.

**Fills carry the pre-trade mid.** The gateway records the mid in force before each message and attaches it to both sides' fills. That single number makes spread capture, markouts and the PnL attribution exact.

**Agents act before the background flow each step.** A step is: update fundamental and regime, let each agent act, process a shuffled batch of background limit orders, market orders and cancels, record. Strategies therefore quote on stale information relative to the flow that trades against them, which is what produces adverse selection.

## Message flow for one order

1. Strategy calls `gateway.limit(...)` or `gateway.market(...)`.
2. Gateway checks risk limits, assigns the next order id, remembers the current mid.
3. Engine stamps a sequence number and calls `book.add`, collecting trades.
4. Engine updates volume, signed volume and notional, optionally appends to the tape.
5. Gateway converts each trade into a maker `Fill` and a taker `Fill` and calls the owners' `on_fill`.
6. Strategies update their `Portfolio` from the fills.
