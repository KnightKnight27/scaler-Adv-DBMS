# System Design Docs

Four short studies of how production databases are actually built — done
hands-on so the numbers in each section come from a real run, not
folklore. The structure of every section is the same:

1. **Problem background** — what the design is solving for.
2. **Architecture overview** — where the pieces live.
3. **Internal design** — the data structures and algorithms that matter.
4. **Trade-offs** — what was given up to get the property above.
5. **Experiments / observations** — what I measured, with numbers.
6. **Key learnings** — what changed in my head after running it.

| Section | What it answers |
|---|---|
| [PostgreSQL_vs_SQLite](./PostgreSQL_vs_SQLite) | Why are these the same SQL but feel totally different in production? Process model, page layout, query planner, concurrency. |
| [PostgreSQL_Internals](./PostgreSQL_Internals) | What does a row look like inside a heap page? How does MVCC manifest as `xmin` / `xmax` / `ctid`? When does VACUUM actually matter? |
| [MySQL_InnoDB](./MySQL_InnoDB) | The clustered-index trade-off, secondary index back-references, undo-log MVCC, and the gap-lock behaviour that makes `REPEATABLE READ` actually mean it. |
| [RocksDB](./RocksDB) | The LSM amplification triangle — what *exactly* is the cost of "writes are cheap"? Measured under leveled vs universal compaction. |

Everything below the top-level READMEs is reproducible: `setup.sql` files
build the schema, `queries.sql` or `amp_bench.cpp` run the workload,
`results.txt` / `bench_results.txt` are the captured outputs.

Same toy e-commerce schema across all SQL sections so cross-database
comparisons are like-for-like:

```
users (id, email, country, created_at)         — 20,000 rows
products (id, sku, category, price_cents)      — 500 rows
orders (id, user_id, product_id, qty, ...)     — 200,000 rows
```

Sized so that:

- `country = 'IN'` covers ~20% of `users` — big enough for a hash join
- `user_id = 12345` matches ~10 of 200,000 orders — selective, indexable
- `qty = 3` matches ~30% of orders — too unselective to bother with an index

That gives every section the same three test queries: a parallel
3-table join, a selective index lookup, and a wide predicate that should
fall back to a sequential scan. The interesting bit is what each
database *does* with them.
