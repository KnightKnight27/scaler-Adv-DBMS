# MiniDB Demo Script

Use this flow for the final project demonstration and viva.

## 1. Build And Smoke Test

```powershell
cd C:\Users\lostdecimal27\projects\scaler-Adv-DBMS\MiniDB_Projects\Team_klode
.\build.ps1
.\build\minidb_smoke.exe
```

Expected output:

```text
smoke test passed
```

## 2. Run Built-In Demo

```powershell
if (Test-Path demo_data) { Remove-Item -Recurse -Force demo_data }
.\build\minidb_cli.exe demo_data --demo
```

This demonstrates:

- `CREATE TABLE`
- `INSERT`
- `SELECT`
- `WHERE`
- `JOIN`
- Query plan output
- Selectivity estimation and join-order selection

Expected join plan includes:

```text
plan: index nested loop join using left primary B+ tree; join order=orders -> users; selectivity users=1.00, orders=0.67
```

## 3. Show Index Selection

```powershell
.\build\minidb_cli.exe demo_data
```

Then enter:

```sql
SELECT name FROM users WHERE id = 1;
```

Expected plan:

```text
plan: primary B+ tree index scan
```

Explain that the first column is the primary key and is stored in the B+ tree.

## 4. Show B+ Tree Contents

```sql
INDEX_DEMO users;
```

Explain that keys appear in sorted leaf order and map back to heap row ids.

## 5. Show Delete

```sql
DELETE FROM users WHERE id = 2;
SELECT * FROM users WHERE id = 2;
```

The second query should return no rows.

## 6. Show 2PL And Deadlock Detection

```sql
LOCK_DEMO;
```

Explain:

- T1 holds an exclusive lock on `users`.
- T2 holds an exclusive lock on `orders`.
- T1 waits for `orders`.
- T2 waits for `users`.
- The wait-for graph contains a cycle, so MiniDB reports a deadlock.

## 7. Show Storage Engine Visibility

```sql
STORAGE_DEMO;
```

Explain:

- Tables are stored as `.heap` files.
- The page size is 4096 bytes.
- Page count is derived from heap file size.
- Active and deleted row counts show tombstone behavior after deletes.
- Heap file reads and writes go through the page manager and buffer pool.

## 8. Show WAL Recovery

The smoke test creates a WAL-only crash scenario:

- Transaction 99 inserts a row and commits.
- Transaction 100 inserts a row but does not commit.
- Restarting MiniDB replays transaction 99 and ignores transaction 100.

Relevant test file:

`tests/smoke_test.cpp`

## 9. Run Benchmark Script

```powershell
.\benchmarks\run_benchmark.ps1
```

Use the output with `benchmarks/BENCHMARK_REPORT.md` during the viva.

## 10. Show Track A Performance Extension

```sql
PERF_DEMO;
```

Explain that both methods evaluate the same predicate on the same synthetic rows. The extension path processes rows in chunks of 128, which is the batch-processing optimization selected for Track A.
