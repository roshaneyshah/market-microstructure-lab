# Phase 4: Quantitative Execution

## Problem

Buy (or sell) a parent order of `X` units over `N` steps. Each step the algorithm picks a child order size and sends it as a market order. Every algorithm is written as a target *cumulative* fill by the end of each step, so if a child order is only partly filled, the next step catches up automatically.

## Algorithms

| Algorithm | Child order at step j |
|---|---|
| **TWAP** | Cumulative target `X (j+1) / N` |
| **VWAP** | Cumulative target `X * sum_{i<=j} w_i`, where `w` is the expected intraday volume profile |
| **POV(r)** | `r / (1 - r)` times the volume other participants traded in the previous step, so own volume is `r` of the total. Does not force completion |
| **Almgren-Chriss** | Mean-variance optimal holdings (below) |

### Almgren-Chriss (2000)

With linear temporary impact `h(v) = epsilon sgn(v) + eta v`, linear permanent impact `g(v) = gamma v`, and price volatility `sigma`, minimising `E[cost] + lambda Var[cost]` over holdings `x_0 = X, ..., x_N = 0` gives

```
x_j = X sinh(kappa (T - t_j)) / sinh(kappa T)

eta_tilde   = eta - gamma tau / 2
kappa_tilde^2 = lambda sigma^2 / eta_tilde
(2 / tau^2)(cosh(kappa tau) - 1) = kappa_tilde^2
```

and

```
E = gamma X^2 / 2 + epsilon sum |n_j| + (eta_tilde / tau) sum n_j^2
V = sigma^2 tau sum_{j=1..N} x_j^2
```

`lambda = 0` gives the linear TWAP path. Larger `lambda` front-loads the trading: more expected impact in exchange for less exposure to price risk. `kappa T` is a convenient unitless urgency, so the experiment picks `lambda` from a target `kappa T` of 0.5, 2, 5 and 10.

Tests check the discrete `kappa` equation, that the trajectory sums to `X`, that the frontier is monotone in `lambda`, and that perturbing the optimal path never lowers the objective.

## Metrics

All costs are signed so that positive is worse for the trader.

| Metric | Definition |
|---|---|
| Implementation shortfall | `[sum side (p_i - m_0) q_i + side (m_T - m_0)(X - filled)] / (X m_0)` in bps. Includes the opportunity cost of unfilled units |
| Slippage vs VWAP | `side (avg price - market VWAP) / market VWAP` in bps. Market VWAP excludes the agent's own trades |
| Fill rate | filled / X |
| Execution cost | `side (avg price - m_0) * filled`, ticks |
| Market impact | `side (m_T - m_0)`, ticks |
| Permanent impact | `side (m_end_of_sim - m_0)`, ticks, measured 50 steps after the horizon |
| Execution-risk distribution | mean, standard deviation, p95 and CVaR95 of shortfall across seeds |

## Experiment

`mml_execution`: buy 600 units over 500 steps (about 17% of expected market volume) in a market with seasonality amplitude 1.5. First the impact model is calibrated from the simulator (see [simulation.md](simulation.md)). Then every algorithm runs on the same 500 seeds. Seed `s` gives every algorithm the same background randomness, so differences between algorithms are not noise from different markets.

Results ([`results/execution_output.txt`](../results/execution_output.txt), [`results/execution_summary.csv`](../results/execution_summary.csv)):

| Algorithm | IS mean (bps) | IS sd | IS p95 | IS CVaR95 | vs VWAP (bps) | Fill |
|---|---:|---:|---:|---:|---:|---:|
| TWAP | 3.92 | 7.36 | 16.21 | 20.16 | 1.31 | 1.000 |
| VWAP | 3.53 | 6.01 | 12.93 | 15.51 | 0.94 | 1.000 |
| POV(20%) | 4.04 | 5.49 | 12.39 | 15.10 | 1.06 | 0.996 |
| AC(kT=0.5) | 3.94 | 6.98 | 14.10 | 18.97 | 1.31 | 1.000 |
| AC(kT=2) | 3.47 | 5.67 | 12.51 | 16.04 | 0.82 | 1.000 |
| AC(kT=5) | 3.84 | 3.80 | 9.76 | 12.40 | -0.13 | 1.000 |
| AC(kT=10) | 4.05 | 2.76 | 8.09 | 9.86 | -0.37 | 1.000 |

With a price near 10,000 ticks, 1 bp is about 1 tick per unit.

### Model vs simulation

Ticks per unit of the parent order:

| Schedule | E model | sd model | E simulated | sd simulated |
|---|---:|---:|---:|---:|
| TWAP | 3.84 | 6.25 | 3.92 | 7.36 |
| AC(kT=0.5) | 3.84 | 6.15 | 3.94 | 6.98 |
| AC(kT=2) | 3.85 | 5.09 | 3.47 | 5.67 |
| AC(kT=5) | 3.88 | 3.41 | 3.84 | 3.79 |
| AC(kT=10) | 3.94 | 2.40 | 4.05 | 2.76 |

### Findings

- **The risk side of Almgren-Chriss holds up well.** Going from TWAP to AC(kT=10) cuts shortfall standard deviation from 7.4 to 2.8 bps and CVaR95 roughly in half, while the mean moves by only about 0.1 bp. The model predicts the direction and approximate size of the variance reduction. Simulated risk is about 15% higher than predicted, consistent with volatile regimes that a single `sigma` cannot represent.
- **Expected cost is mostly fixed.** The model says roughly 3.8 of the 3.9 ticks per unit is unavoidable: spread crossing (`epsilon`) plus permanent impact `gamma X / 2`. Scheduling only changes the small `eta` term. So at this order size the real choice between schedules is about risk, not mean cost.
- **VWAP beats TWAP when volume is seasonal.** Trading into the high-volume open and close lowers both mean shortfall and VWAP slippage, because liquidity is deeper at those times.
- **POV has the lowest risk of the benchmark algorithms but leaves quantity unfilled** (99.6% fill), because it only follows realised volume. The unfilled part is charged at the end-of-horizon price.
- **Front-loaded schedules beat VWAP as a benchmark** (negative slippage for AC kT=5 and 10). This is a benchmark effect, not skill: buying early pushes up the price that the rest of the market's VWAP is then measured at.
- **Mean differences are small relative to noise.** With 500 seeds the standard error of each mean is about 0.1 to 0.3 bp, so the ordering of the means (as opposed to the standard deviations) should not be over-interpreted.

## Reproduce

```
./build/mml_execution --seeds 500
./build/mml_execution --quantity 1500 --horizon 300 --side sell --seasonality 0
```

Outputs: `results/impact_calibration.csv`, `execution_runs.csv` (one row per seed and algorithm), `execution_summary.csv`, `ac_frontier.csv`.
