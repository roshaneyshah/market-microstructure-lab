# Phase 1: Exchange Core

## Order types

| Type | Behaviour |
|---|---|
| Limit GTC | Matches against the opposite side up to its limit price; any remainder rests |
| Limit IOC | Matches what it can immediately; remainder cancelled |
| Limit FOK | Executes only if the whole quantity is available within the limit; otherwise does nothing |
| Market | Matches at any price until filled or the opposite side is empty; never rests |

## Matching rules

- **Price priority.** Best price first: highest bid, lowest ask.
- **Time priority.** Within a price, the oldest order trades first (FIFO).
- **Trade price.** Trades print at the resting (maker) order's price, so an aggressive order can receive price improvement.
- **Partial fills.** A maker that is partially filled keeps its place at the head of its queue.

## Modify semantics

`modify(id, new_price, new_quantity)` sets the new *open* quantity.

- Same price and quantity reduced (or unchanged): done in place, **queue priority kept**.
- Price changed or quantity increased: cancel and re-add with the same id, **priority lost**. If the new price crosses the spread, the order trades immediately.

This matches common exchange practice: you cannot jump the queue by growing an order.

## Result statuses

| Status | Meaning |
|---|---|
| `Accepted` | Rested without trading |
| `PartiallyFilled` | Traded some, remainder rests |
| `Filled` | Fully traded |
| `Cancelled` | Remainder not rested (IOC, FOK, market). `filled` may be non-zero |
| `Rejected` | Validation failed. The book is unchanged |

Reject reasons: invalid or duplicate id, non-positive quantity, invalid price (non-positive, or outside the ladder range), unknown order on modify, risk limit and wrong owner (gateway).

## Data structures

```
BasicOrderBook
 ├─ Levels<Buy>   bids   best = highest
 ├─ Levels<Sell>  asks   best = lowest
 ├─ Index               OrderId -> OrderNode*
 └─ ObjectPool<OrderNode>

PriceLevel { price, total, count, head, tail }
OrderNode  { id, trader, side, price, remaining, ts, prev, next, level }
```

| Operation | Map levels | Ladder levels |
|---|---|---|
| Add at existing level | O(log L) find + O(1) append | O(1) |
| Add at new level | O(log L) + allocation | O(1) |
| Cancel | O(1) after id lookup (+ O(log L) if the level empties) | O(1) (+ scan to the next level if best empties) |
| Best bid/ask | O(1) | O(1) |
| Match k orders | O(k) plus level removals | O(k) plus scans |

L is the number of price levels. The ladder trades memory proportional to the tick range for constant-time access.

## Correctness checks

- `tests/test_order_book.cpp`: every scenario runs against all four book variants.
- `tests/test_fuzz.cpp`: differential fuzzing. Twelve random streams of 3,000 messages (adds of all types, cancels, modifies, invalid orders) are replayed into the four variants and a deliberately naive O(n) reference book. Every result, every trade and every depth snapshot must match, and `check_invariants()` (linked-list integrity, level aggregates, index consistency, uncrossed book) must hold throughout.
- CI runs the suite under AddressSanitizer and UndefinedBehaviorSanitizer with GCC and Clang.

## Try it

```
./build/mml_demo
```

walks through building a book, price-time priority, partial fills, a market sweep, IOC vs FOK, modify and cancel, printing the book and every fill.
