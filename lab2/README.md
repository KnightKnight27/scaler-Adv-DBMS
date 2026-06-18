# Lab 2: Comparing SQLite3 and PostgreSQL Performance

**Student ID:** `24BCS10126`

**Name:** Manav Pratap Singh

---

## 1. SQLite3

### The Setup

For this test, I used a local database named `lab2.db`. Inside it, I built a standard `users` table and filled it up with 100,000 placeholder records.

### Terminal Commands Run

```bash
# Set up the DB, enable WAL mode, and generate 100k rows on the fly
sqlite3 lab2.db "PRAGMA journal_mode=WAL;
  CREATE TABLE IF NOT EXISTS users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
  );
  DELETE FROM users;
  WITH RECURSIVE c(x) AS (
    SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < 100000
  )
  INSERT INTO users(name, email)
  SELECT 'user_'||x, 'user_'||x||'@example.com' FROM c;"

# Take a look at the resulting file storage
ls -lh lab2.db lab2.db-wal lab2.db-shm

# Check out internal page sizes, counts, and the mmap configuration
sqlite3 lab2.db "PRAGMA page_size; PRAGMA page_count; PRAGMA mmap_size;"

# Benchmark 1: Query speed without memory mapping
time sqlite3 lab2.db "PRAGMA mmap_size=0; SELECT * FROM users;" > /dev/null

# Benchmark 2: Query speed with a 256 MB memory map turned on
time sqlite3 lab2.db "PRAGMA mmap_size=268435456; SELECT * FROM users;" > /dev/null

# Make sure no background sqlite processes are hanging around
ps aux | grep sqlite
```

### What the Data Shows

- **`lab2.db` footprint on disk (`ls -lh`):** `3.9M`
- **`lab2.db-wal` status (after closing connections):** `0B`
- **`PRAGMA page_size` configuration:** `4096` bytes
- **`PRAGMA page_count` total:** `986` pages
- **Default `PRAGMA mmap_size` allocation:** `0`
- **Scanning `users` table without mmap:** `real 0m0.049s`
- **Scanning `users` table with 256 MB mmap active:** `real 0m0.040s`

> **My takeaway on memory mapping (`mmap`):** Flipping on `mmap` shaved roughly 18% off the query time. Instead of relying on traditional, heavy-handed filesystem `read()` calls, SQLite maps the database file straight into the process's address space. This allows the OS page cache to handle data retrieval directly, cutting out the typical data-copy overhead between kernel and user space. For small, already-cached datasets like this one, the speed bump is helpful but modest. It really shines when dealing with massive databases or working with a completely cold system cache.
> 

---

## 2. Setting Up and Testing PostgreSQL

### The Setup

- **Engine Version:** `PostgreSQL 16.3`
- **Target Data:** A table named `users_lab2` initialized with 100,000 rows.

### Terminal Commands Run

```bash
# Make sure the server version matches up
psql -d postgres -c "SELECT version();"

# Drop any old tables, create a fresh schema, and pipe in 100k entries
psql -d postgres -c "
  DROP TABLE IF EXISTS users_lab2;
  CREATE TABLE users_lab2 (
    id   BIGINT PRIMARY KEY,
    name TEXT,
    email TEXT
  );
  INSERT INTO users_lab2
    SELECT g, 'user_'||g, 'user_'||g||'@example.com'
    FROM generate_series(1, 100000) g;
  ANALYZE users_lab2;
"

# Pull the database block size
psql -d postgres -At -c "SHOW block_size;"

# Check how many pages and rows Postgres tracks in its system catalog
psql -d postgres -c "
  SELECT relpages, reltuples::bigint
  FROM pg_class
  WHERE relname = 'users_lab2';
"

# Get a breakdown of internal server execution timing
psql -d postgres -c "EXPLAIN ANALYZE SELECT * FROM users_lab2;"

# Measure the total round-trip time from the client side
time psql -d postgres -c "SELECT * FROM users_lab2;" > /dev/null
```

### What the Data Shows

- **Value returned by `SHOW block_size`:** `8192` bytes
- **Number of pages (`relpages`):** `842` pages
- **Total rows tracked (`reltuples`):** `100000`
- **Pure execution time via `EXPLAIN ANALYZE`:** `~8.2 ms`
- **Total client-side wall clock time (`time psql ...`):** `real 0m0.161s`

> **A quick note on performance metrics:** Don't confuse the two timings here. The `EXPLAIN ANALYZE` metric shows only how long the server engine took to run the query internally. The client wall time is naturally higher because it includes the entire overhead of establishing the connection, handling TCP sockets, formatting the results, and sending them to stdout.
> 

---

## 3. Side-by-Side Comparison

| Evaluation Metric | SQLite3 | PostgreSQL |
| --- | --- | --- |
| **Default Block/Page Size** | 4096 bytes | 8192 bytes |
| **Page Counts (for 100k rows)** | 986 pages (`PRAGMA page_count`) | 842 pages (`relpages` via `pg_class`) |
| **`SELECT *` Processing Speed** | ~0.049s (Standard disk reads) | ~8.2 ms (Server internals) / ~0.161s (Client lifecycle) |
| **How Memory Mapping works** | Explicitly configured with `PRAGMA mmap_size` | Handled implicitly via the OS cache and `shared_buffers` |
| **Performance Gain from `mmap**` | Yielded about an 18% speedup at 256 MB | Not a configurable option on a per-query level |
| **Underlying Architecture** | Serverless, completely embedded, single-file | True client-server model utilizing independent processes |
| **System Insights & Observability** | Evaluated via `PRAGMA` flags and `sqlite_stat*` | Evaluated via `EXPLAIN ANALYZE` and system catalogs |

### Deep Dive Analysis

- **Page Sizing Trade-offs:** Postgres sets its default data blocks to 8 KB, which is exactly double the 4 KB architecture SQLite uses. Having larger blocks is highly advantageous for running big sequential scans and keeping index trees relatively shallow. On the flip side, SQLite’s smaller 4 KB page profile mirrors the native page allocation size of most modern operating systems, which keeps simple single-page file modifications incredibly efficient and low-overhead.
- **Storage Density Differences:** Even though both engines are storing identical chunks of 100k records, Postgres packs it all into 842 pages compared to SQLite's 986 pages. This difference comes down to the larger block boundaries, allowing Postgres to squeeze significantly more rows into individual pages.
- **Latency Profile:** Both database engines deal with 100k records effortlessly. Because SQLite runs directly within the host application process, it entirely avoids any Inter-Process Communication (IPC) overhead, keeping local processing speeds exceptionally high. Postgres's query planner executes highly efficiently on the server side; the slight delay you see in client wall time is purely the natural tax of moving data across an isolated network connection.
- **Memory Management Approaches:** SQLite puts memory management directly in the developer's hands using `PRAGMA mmap_size`. This shifts file interactions away from classic system calls to a page-fault structure, preventing an unneeded extra hop through memory buffers. Postgres approaches this with a global, dedicated `shared_buffers` architecture, leaving deeper filesystem-level caching up to the host OS rather than exposing a direct session switch.

---

## 4. Final Verdict

- SQLite is the go-to architecture for embedded software, desktop application storage, or single-user environments. It offers zero setup friction, negligible resource usage, and predictable memory mapping control.
- PostgreSQL is tailored for high-concurrency, enterprise infrastructure where massive user scale, extensive catalog metrics (`pg_stat`), and strict transactional controls are required.
- For straightforward tasks like running sequential row scans on moderate 100k data frames, both setups provide sub-second latency. The deciding factor isn't raw performance at this level—it depends entirely on whether your project benefits from a local file-backed system or a robust client-server design.
- Adjusting `mmap` parameters inside SQLite provides a clear performance bump when data is sitting warm in memory, an advantage that scales up as the underlying database footprint expands.