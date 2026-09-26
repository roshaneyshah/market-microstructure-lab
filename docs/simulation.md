# Phase 3: Market Simulation

## Model

The simulator is a discrete-time, agent-based market built on the real matching engine. Nothing about prices, spreads or impact is imposed directly. They emerge from order flow interacting with the book.

**Latent fundamental.** A value `F` follows a random walk with volatility set by a two-state Markov regime:

```
F_{t+1} = F_t + sigma(regime_t) * Z_t + lambda_perm * (net aggressive volume in step t)
```

- Calm regime: sigma = 0.35 ticks/step. Volatile regime: sigma = 1.1, with market orders arriving 1.6x faster.
- Transitions: calm to volatile with probability 0.002 per step, back with 0.02 (volatile about 9% of the time).
- `lambda_perm` (default 0.01 ticks per unit) is a Kyle-style permanent impact: all aggressive volume, including strategies' own, moves the fundamental.

**Liquidity providers.** Limit orders arrive as a Poisson process (6 per step). Buys are placed at `floor(F - 0.5) - G`, sells at `ceil(F + 0.5) + G`, where `G` is geometric (p = 0.3). Sizes are uniform on 1 to 6. When `F` moves, new orders cross stale ones, which is how the book discovers the fundamental.

**Liquidity takers.** Market orders arrive as a Poisson process (0.8 per step), sizes uniform on 1 to 12. The direction is tilted toward the fundamental:

```
P(buy) = 0.5 + 0.5 * 0.6 * tanh((F - mid) / 2)
```

so takers are partially informed. This informed flow is the source of adverse selection for market makers.

**Cancellations.** Each resting background order is cancelled with hazard 0.05 per step, so the book reaches a steady state of roughly 80 orders.

**Intraday seasonality.** Arrival rates can be multiplied by a U-shaped profile with mean 1:

```
activity(x) = 1 + a * ((2x - 1)^2 - 1/3),   x = time of day in [0, 1]
```

**Step order.** Update the regime and the fundamental, let agents act, process a shuffled batch of limit, market and cancel events, record. Agents see the market as of the start of the step.

**Reproducibility.** `Rng` is xoshiro256** with hand-written uniform, normal (Box-Muller), Poisson, geometric and exponential draws, so a seed gives identical results on every platform. The background flow has its own RNG stream, so different strategies run with the same seed face the same background randomness (common random numbers).

## Emergent statistics (default parameters)

From 3 seeds x 5,000 steps with no strategy present:

| Statistic | Value |
|---|---|
| Mean quoted spread | about 1.7 ticks |
| Volume per step | about 5.6 units |
| Mid volatility (1 step) | about 0.39 ticks |
| Resting orders | about 80 |
| Top-of-book depth (both sides) | about 32 units |

## Market-impact model

`calibrate_impact()` measures impact inside the simulator. A probe agent sends market orders of 2 to 40 units every 25 steps, alternating random sides, and records:

- **Temporary cost per unit**, `side * (average fill - mid before)`, regressed on order size: `cost = epsilon + eta * q`.
- **Permanent impact**, `side * (mid 10 steps later - mid before)`, regressed through the origin: `gamma * q`.
- **Volatility** from 20-step mid returns (reduces bid-ask bounce).
- **Square-root law** coefficient `Y` in `cost = Y * sigma * sqrt(q / V)`.

Result over 5 seeds (800 probes), from [`results/execution_output.txt`](../results/execution_output.txt):

| Parameter | Estimate |
|---|---|
| sigma | 0.485 ticks / sqrt(step) |
| epsilon (fixed cost, about half the spread) | 0.875 ticks |
| eta (linear temporary) | 0.0259 ticks per unit, R^2 = 0.24 |
| gamma (permanent) | 0.0098 ticks per unit |
| Square-root law Y | 1.70 |

The recovered `gamma` (0.0098) matches the configured `lambda_perm` of 0.01. That is a useful sanity check that the calibration measures the mechanism it is meant to measure.

## Next steps

- Hawkes (self-exciting) arrivals instead of Poisson, to reproduce clustered order flow.
- Queue-reactive order flow (Huang, Lehalle and Rosenbaum), where arrival rates depend on queue sizes.
- Agent latency, so strategies act on delayed data.
