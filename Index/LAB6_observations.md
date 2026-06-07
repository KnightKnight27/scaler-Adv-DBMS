# Lab 6: B+ Tree Index — Observations

## B-Tree vs B+ Tree Rationale

The assignment describes a **B-Tree** index; this implementation uses a **B+ Tree**, which is the variant used by SQLite, PostgreSQL, and most production database engines.

| Aspect | B-Tree | B+ Tree (this lab) |
| :--- | :--- | :--- |
| Data location | Keys + values in internal and leaf nodes | Values **only in leaves**; internal nodes hold routing keys |
| Leaf linking | None | Sibling **leaf chain** for range scans |
| Internal keys | May duplicate leaf keys | Copies of leaf separators for navigation |
| Range queries | Tree re-traversal | Walk leaf chain after one descent |

B+ Trees keep internal nodes small (more keys per page on disk), maximize fan-out, and make sequential/range access efficient — the same reasons DBMS index pages are organized as B+ variants.

## Insertion and Splitting

With `min_degree = 2` (max 3 keys per node), inserting ascending keys `1..30` triggers leaf splits when a node fills. The median key is promoted to the parent; tree height grows only when the root splits. `validate()` confirms all leaves sit at the **same depth** after every insert in the test suite.

## Search Behavior (Database Index Analogy)

Point search descends from root to a single leaf — **O(log n) node accesses**. For a shallow tree over 500 keys, searches typically visit 2–4 nodes (root internal + leaf), mirroring how a database index avoids full table scans:

- Each internal level narrows the search space using sorted separator keys (like index interior pages in SQLite Lab 4).
- The leaf holds the actual row pointer/value (like `(key → rowid)` in `sqlite_autoindex_students_1`).
- Range search uses the leaf `next` pointer — same idea as scanning index leaves in key order.

The CLI `search` command reports **nodes visited**, illustrating the I/O savings of a balanced multi-way tree versus a linear scan.

## Build and Test

```bash
cd Index && mkdir -p build && cd build
cmake .. && make && ./Btree --run-tests
```
