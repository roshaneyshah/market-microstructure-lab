# Market Microstructure Lab

A high-performance **C++20 market microstructure and electronic exchange simulator** for exploring limit order books, order matching, execution algorithms, and quantitative trading strategies.

The project is being built from the ground up with an emphasis on **correctness, performance, realistic exchange mechanics, and reproducible quantitative experiments**.

> **Status:** 🚧 Early development

## Overview

Modern electronic markets operate through limit order books where participants continuously submit, cancel, and execute orders.

Market Microstructure Lab aims to recreate the core mechanics of this process in C++, providing an environment for studying questions such as:

* How should orders be efficiently represented and matched?
* How does price-time priority affect execution?
* How do liquidity and order flow affect execution quality?
* What is the cost of executing a large order?
* How do market makers balance spread capture against inventory risk?
* How do latency and data-structure choices affect exchange performance?

Rather than functioning as a production trading system, this repository is intended as a **research and experimentation platform** for quantitative finance, market microstructure, and high-performance C++.

## Planned Architecture

```text
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

## Roadmap

### Phase 1 — Exchange Core

* [ ] Order representation
* [ ] Bid and ask books
* [ ] Limit orders
* [ ] Market orders
* [ ] Price-time priority
* [ ] Partial fills
* [ ] Order cancellation
* [ ] Order modification
* [ ] Trade generation
* [ ] Unit tests

### Phase 2 — Performance

* [ ] C++ benchmarking suite
* [ ] Matching latency measurements
* [ ] Throughput measurements
* [ ] Efficient order-ID lookup
* [ ] Memory allocation analysis
* [ ] Alternative order-book data structures
* [ ] Cache-performance investigation

### Phase 3 — Market Simulation

* [ ] Synthetic order-flow generator
* [ ] Configurable spread and liquidity
* [ ] Stochastic market-order arrivals
* [ ] Limit-order arrivals and cancellations
* [ ] Volatility regimes
* [ ] Market-impact model

### Phase 4 — Quantitative Execution

Implement and compare execution algorithms including:

* [ ] TWAP
* [ ] VWAP
* [ ] Percentage of Volume (POV)
* [ ] Almgren–Chriss optimal execution

Evaluation will include:

* Implementation shortfall
* Slippage
* Fill rate
* Execution cost
* Market impact
* Execution-risk distribution

### Phase 5 — Market Making

* [ ] Baseline symmetric market maker
* [ ] Inventory tracking
* [ ] Inventory-aware quoting
* [ ] Avellaneda–Stoikov-inspired strategy
* [ ] Order-flow imbalance signals
* [ ] Adverse-selection analysis
* [ ] Transaction fees and rebates
* [ ] Strategy P&L attribution

## Technical Goals

The exchange core will primarily use **modern C++20**.

Several implemen
