# Track A Benchmark — Columnar / Vectorised Execution

## What is measured

The same analytical workload — the total of `amount` over rows where
`region < 5` — run two ways against identical data:

```sql
SELECT amount FROM sales WHERE region < 5;
```

- **Row engine** — the normal MiniDB path: a heap scan that deserialises every
  tuple, a `Filter`, and a `Project`, all through the Volcano iterator; the
  harness then sums the returned values.
- **Columnar + vectorised** — Track A: the `amount` and `region` columns are
  stored contiguously, and the sum is computed in batches of 1024 values over
  that contiguous memory, touching only the two columns the query needs.

Both paths compute the same answer; the benchmark asserts they match.

Reproduce with:

```bash
./build/bench_columnar <row_count>
```

## Results

Measured on Apple clang `-O2`, release build. `region` has 10 distinct values,
so the predicate `region < 5` selects ~50% of rows.

| Rows | Row engine | Columnar + vectorised | Speedup |
|------|-----------:|----------------------:|--------:|
| 50,000 | 42.2 ms | 0.10 ms | ~430× |
| 100,000 | 78.0 ms | 0.23 ms | ~339× |
| 200,000 | 154.1 ms | 0.57 ms | ~269× |

(Absolute numbers vary by machine; the ratio is the point.)

## Why columnar is faster here

1. **Only the needed columns are read.** The row engine decodes the whole tuple
   (`id`, `region`, `amount`) for every row; the columnar path reads just
   `region` and `amount`.
2. **No per-tuple deserialisation.** The row path turns bytes into a `Tuple` of
   `Value`s for each row. The columnar path operates directly on `int64`
   arrays.
3. **Contiguous, batched access.** Summing a contiguous `std::vector<int64_t>`
   in 1024-value batches is cache-friendly and lets the compiler vectorise the
   inner loop, versus pulling one `Row` at a time through virtual `next()` calls.

## Trade-off

Columnar layout is optimised for analytical scans over few columns. It is worse
for point inserts and for queries that need whole rows, which is exactly why
MiniDB keeps the row-oriented heap as its primary store and uses the columnar
representation as a read-optimised path. This row-vs-column trade-off is the
core lesson of the extension.
