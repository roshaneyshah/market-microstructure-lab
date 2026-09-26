# Phase 5: Market Making

## Framework

`MarketMaker` is an `Agent`. Once per step it:

1. marks its portfolio to the current mid,
2. updates order-flow signals from the book,
3. asks the strategy for a bid and an ask (price and size, or none),
4. reconciles live orders with that quote.

An order whose price has not changed is left alone, so it keeps its queue position. A changed price is cancelled and replaced. Quotes are **post-only**: they are clamped so they never cross the book, and every fill is a maker fill.

## Strategies

| Strategy | Quote |
|---|---|
| **Symmetric** | `bid = floor(mid - h)`, `ask = ceil(mid + h)`. `h = 0.5` joins the touch; `h = 1.5` quotes wide |
| **Inventory skew** | Both quotes shifted by `-k q` (`q` = inventory). The side that would breach `max_inventory` is dropped |
| **Avellaneda-Stoikov** | Reservation price and spread from Avellaneda and Stoikov (2008), below |
| **AS + imbalance** | AS with the reservation price shifted by `alpha * I`, where `I` is the queue imbalance |

### Avellaneda-Stoikov

```
r     = s - q gamma sigma^2 H
delta = gamma sigma^2 H + (2 / gamma) ln(1 + gamma / k)
bid   = floor(r - delta / 2),  ask = ceil(r + delta / 2)
```

`q` is inventory in quote-size lots, `gamma` is risk aversion, `k` is how fast fill intensity decays with distance from the mid, and `sigma` is estimated online with an EWMA of mid changes. The original model uses the time to a terminal date, `T - t`. This implementation uses a rolling horizon `H` (100 steps), the usual stationary approximation for a continuously running market maker. Defaults: `gamma = 0.1`, `k = 1.5`, 5-unit quotes, inventory limit 60.

## Signals

- **Queue imbalance** `I = (q_bid - q_ask) / (q_bid + q_ask)` at the touch.
- **Order-flow imbalance** (Cont, Kukanov and Stoikov 2014), accumulated from successive top-of-book snapshots, smoothed with an EWMA and normalised by average depth.

## Fees and rebates

`FeeSchedule` sets per-unit maker and taker fees in ticks; negative means a rebate. The experiment uses a maker rebate of 0.1 and a taker fee of 0.3.

## PnL attribution

`Portfolio` splits mark-to-mid PnL into three parts with **no residual term**:

```
spread capture = sum over fills  side * (mid at fill - fill price) * qty
inventory PnL  = sum over time   position * change in mid
fees           = fees paid (negative if net rebates)
total          = spread capture + inventory PnL - fees  = cash + position * mid
```

The identity is exact because every fill carries the mid in force when it happened, and the portfolio marks to that mid before applying the fill. A randomised test checks it on thousands of fill sequences, alongside the average-cost identity `realized + unrealized - fees = total`.

## Adverse selection: markouts

For each maker fill at price `p` with reference mid `m`, and mid `m_h` after `h` more steps:

```
markout = side * (m_h - p) = side * (m - p)   +   side * (m_h - m)
                             spread capture       adverse move
```

A negative adverse move means fills come just before the price moves against the maker. Markouts are also split by the volatility regime at the time of the fill.

## Results

20 seeds x 5,000 steps per strategy, same seeds for every strategy. From [`results/market_making_output.txt`](../results/market_making_output.txt).

### PnL attribution (ticks per run, mean over seeds)

| Strategy | Total | sd | Spread | Inventory | Fees | Fills | max\|q\| | avg\|q\| | Win % |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Symmetric (join) | 1,172 | 4,950 | 5,151 | -4,758 | -778 | 2,458 | 190.8 | 78.5 | 75% |
| Symmetric (wide) | 1,505 | 1,831 | 2,169 | -807 | -143 | 458 | 99.0 | 45.1 | 80% |
| Inventory skew | 2,430 | 374 | 4,496 | -2,633 | -567 | 1,743 | 22.8 | 3.5 | 100% |
| Avellaneda-Stoikov | 1,259 | 139 | 1,829 | -728 | -158 | 469 | 8.9 | 1.3 | 100% |
| AS + imbalance | 944 | 100 | 1,554 | -753 | -144 | 423 | 7.7 | 1.3 | 100% |

### Markouts (ticks per unit)

| Strategy | Capture | Adverse @1 | Adverse @20 | Markout @20 | Adverse @20, calm | Adverse @20, volatile |
|---|---:|---:|---:|---:|---:|---:|
| Symmetric (join) | 0.662 | -0.335 | -0.523 | 0.139 | -0.367 | -1.311 |
| Symmetric (wide) | 1.517 | -0.432 | -0.571 | 0.946 | -0.244 | -1.146 |
| Inventory skew | 0.793 | -0.374 | -0.479 | 0.314 | -0.345 | -1.064 |
| Avellaneda-Stoikov | 1.157 | -0.473 | -0.478 | 0.680 | -0.347 | -1.126 |
| AS + imbalance | 1.081 | -0.462 | -0.512 | 0.570 | -0.370 | -1.130 |

### Queue-imbalance signal

Correlation of `I_t` with the next `h`-step mid change, on markets with no strategy present (about 50,000 observations):

| h | Correlation | Slope (ticks per unit I) | R^2 |
|---|---:|---:|---:|
| 1 | 0.189 | 0.151 | 0.036 |
| 5 | 0.080 | 0.134 | 0.006 |
| 20 | -0.040 | -0.156 | 0.002 |

### Findings

- **Inventory control is what makes market making viable here.** The symmetric maker that joins the touch captures the most spread (5,151 ticks) but gives back 4,758 to inventory swings. Its PnL standard deviation is 4,950, larger than its mean, and it loses in a quarter of runs. Adding a linear skew keeps average inventory at 3.5 units instead of 78.5, turns the same flow into the highest mean PnL, and wins every run.
- **Avellaneda-Stoikov gives the best risk-adjusted result.** It trades less than the skew maker, so its mean PnL is lower, but its PnL sd (139) is the smallest and its inventory rarely exceeds 9 units. The ratio of mean to sd is about 9, against 6.5 for the skew maker and 0.2 for the symmetric joiner.
- **Adverse selection is real and depends on the regime.** Every strategy loses 0.3 to 0.5 ticks per unit to adverse moves within a step, and the loss per unit is three to five times larger in the volatile regime. That is the informed taker flow at work. Wider quotes earn more spread per fill but do not escape adverse selection.
- **Rebates matter at the margin.** The 0.1-tick maker rebate adds 144 to 778 ticks per run, roughly 10 to 65% of total PnL depending on the strategy.
- **The imbalance signal does not help as used here.** Queue imbalance predicts the next step's mid change (corr 0.19), but the effect is gone by 20 steps. Shifting AS quotes by `1.0 * I` lowers PnL (944 vs 1,259). The shift moves quotes toward the side that is about to be hit, so the maker trades less and captures less spread, and the one-step edge is too small to pay for that. A better use would be to pull the quote on the threatened side instead of shifting both.

## Reproduce

```
./build/mml_market_making --seeds 20 --steps 5000
./build/mml_market_making --maker-fee 0 --taker-fee 0
```

Outputs: `results/mm_runs.csv`, `mm_summary.csv`, `mm_markouts.csv` (by regime and horizon), `signal_study.csv`.
