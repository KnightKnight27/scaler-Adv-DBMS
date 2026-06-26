# System Design: Analyze DuckDB vs PostgreSQL

---

## 1. What & Why

**Row-Oriented Storage (PostgreSQL)** stores each record as a contiguous tuple — all fields of a row sit together in memory. This is great for OLTP workloads where you frequently read/write entire rows (INSERT, UPDATE, point lookups by primary key).

**Columnar Storage (DuckDB)** stores each column as a separate contiguous array. When an analytical query only needs one column (e.g. `SUM(salary)`), it reads just that array — nothing else. This gives dramatically better cache utilization and enables vectorized processing, which is why DuckDB and other OLAP engines use it.

The goal here is to build both layouts from scratch and benchmark them to see the difference firsthand.

---

## 2. Comparison

| Aspect | Row Store (PostgreSQL) | Column Store (DuckDB) |
|---|---|---|
| Layout | All fields of a row stored together | Each column stored as a separate array |
| Best for | OLTP — inserts, updates, point queries | OLAP — aggregations, scans, analytics |
| Cache behavior | Pulls unused fields into cache lines | Only relevant column data in cache |
| Compression | Hard — mixed types per cache line | Easy — same type, run-length/dict encoding |
| Full row reads | Fast — row is already contiguous | Slower — must reconstruct from columns |
| Aggregations | Slow — must skip over unused fields | Fast — tight loop over one array |

---

## 3. Project Structure

```
SystemDesign4/
├── RowStore.h        # Row struct + RowStore class declaration
├── RowStore.cpp      # Tuple-at-a-time implementation
├── ColStore.h        # ColStore class declaration
├── ColStore.cpp      # Vectorized columnar implementation
├── main.cpp          # Benchmark comparing both stores
├── Makefile          # Build rules
└── README.md
```

---

## 4. Implementation

### Row struct (shared input format)

```cpp
struct Row {
    int id;
    std::string name;
    std::string department;
    double salary;
};
```

### RowStore — SUM(salary) (tuple-at-a-time)

```cpp
double RowStore::sumSalary() const {
    // Must walk entire row struct just to read salary
    // Each cache line pulls in id, name, department too
    double total = 0.0;
    for (const auto& row : rows_) {
        total += row.salary;
    }
    return total;
}
```

### ColStore — SUM(salary) (vectorized)

```cpp
double ColStore::sumSalary() const {
    // Only the salary column — contiguous doubles in memory
    // No wasted reads, perfect cache locality
    double total = 0.0;
    for (double s : salaries_) {
        total += s;
    }
    return total;
}
```

The loop body is identical. The difference is what's in memory around each value. In `RowStore`, each salary sits inside a Row struct alongside strings. In `ColStore`, salaries are packed together — the CPU prefetcher loves this.

---

## 5. Build & Run

```bash
make
./storage
```

Clean:

```bash
make clean
```

---

## 6. Output

```
=========================================
 Row vs Column Store  —  100000 rows
=========================================

[Insert] 100000 rows loaded into both stores.
  RowStore: 8.578 ms
  ColStore: 12.391 ms

--- Analytical Query Benchmark ---

  Operation                        RowStore       ColStore   Speedup
  ------------------------------------------------------------------
  SUM(salary)                      3.577 ms       0.261 ms     13.68x
  COUNT(dept='Engineering')        1.197 ms       0.514 ms      2.33x
  Full Scan (SELECT *)            18.510 ms      18.781 ms      0.99x

--- Correctness Check ---
  SUM(salary)  row=7950000000.00  col=7950000000.00  ✓ match
  COUNT(Engineering)  row=20000  col=20000  ✓ match

--- Why Columnar Wins for Analytics ---
  SUM(salary): ColStore reads ONLY the salary vector.
    Each cache line is packed with salary values.
    RowStore pulls entire rows (id, name, dept, salary)
    into cache — most of that data is never used.

  Full Scan: RowStore is competitive here because
    every field is needed — no wasted reads.
    ColStore must reconstruct rows from separate columns.

=========================================
```

*Exact numbers vary by machine — the relative speedups are what matter.*

---

## 7. Notes

- The `SUM(salary)` gap is the key insight. RowStore must touch ~80+ bytes per row (two `std::string` objects, an `int`, a `double`) just to read 8 bytes of salary. ColStore reads 8 bytes per row, period.
- Full scan is where row stores shine — the data is already in row format, no reconstruction needed.
- Real columnar engines (DuckDB, ClickHouse) add vectorized execution, SIMD, and compression on top of this layout. We're just showing the memory access pattern advantage.
- In production, PostgreSQL compensates with buffer pool tuning and index-only scans. DuckDB compensates for row-level operations with zone maps and small materialized aggregates. Neither layout is universally better — it depends on the workload.