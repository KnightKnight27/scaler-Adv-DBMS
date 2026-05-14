# SQLite3 Storage Engine Exploration and mmap Benchmarking

## Objective

The objective of this lab was to explore how SQLite internally stores and accesses data using:
- Pages
- Page cache
- mmap (Memory Mapped I/O)
- Query execution timing
- OS virtual memory interaction

The experiment focused on understanding how database systems minimize expensive disk I/O operations.

---

# Dataset Used

Dataset:
- Titanic Dataset CSV

Initial CSV Size:
- ~60 KB

---

# Initial Directory Structure

```bash
ls -lh
```

Output:

```text
AdvDBMS2.pdf
AdvDBMS3.pdf
Titanic-Dataset.csv
```

---

# Step 1 — Creating SQLite Database

Command:

```bash
sqlite3 lab1.db
```

Inside SQLite shell:

```sql
.mode csv
.import Titanic-Dataset.csv titanic
```

Verification:

```sql
.tables
```

Output:

```text
titanic
```

Checking rows:

```sql
SELECT * FROM titanic LIMIT 5;
```

---

# Observation 1 — Database Size Increased

After importing:

| File | Size |
|---|---|
| Titanic-Dataset.csv | ~60 KB |
| lab1.db | ~72 KB |

---

# Why Did Database Size Increase?

CSV only stores:
- Raw text
- Comma-separated values

SQLite stores:
- Fixed-size pages
- B-Tree structures
- Schema metadata
- Headers
- Internal storage-engine structures
- Free space inside pages
- Binary encoded row data

Hence database size became larger than the original CSV.

---

# Step 2 — Exploring SQLite Pages

Commands:

```sql
PRAGMA page_size;
PRAGMA page_count;
```

Output:

```text
page_size  = 4096
page_count = 18
```

Verification:

```text
18 × 4096 = 73728 bytes ≈ 72 KB
```

This matched the actual database size.

---

# Observation 2 — SQLite Stores Data in Pages

SQLite internally stores databases as:
- Fixed-size pages
- Not raw rows

Important formula:

```text
Database Size ≈ page_size × page_count
```

This demonstrated that databases internally operate around pages instead of individual rows.

SQLite documentation also describes databases as page-oriented file structures. :contentReference[oaicite:0]{index=0}

---

# Step 3 — Exploring mmap

Checked current mmap configuration:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

Meaning:
- mmap was disabled by default.

SQLite documentation states mmap is disabled by default on many systems because of platform-specific tradeoffs and risks. :contentReference[oaicite:1]{index=1}

---

# What Happens Without mmap?

Without mmap:

```text
Disk
 ↓
Kernel Page Cache
 ↓ COPY
SQLite Heap Memory
 ↓
Query Execution
```

SQLite primarily uses:
- xRead()
- read() system calls
- kernel-to-user-space copying

SQLite documentation explains that traditional xRead() paths involve copying database pages between kernel space and user space. :contentReference[oaicite:2]{index=2}

---

# Enabling mmap

Enabled mmap:

```sql
PRAGMA mmap_size = 1048576;
```

Later increased to:

```sql
PRAGMA mmap_size = 268435456;
```

(256 MB)

---

# What Happens With mmap?

With mmap enabled:

```text
Disk
 ↓
OS maps pages into virtual memory
 ↓
SQLite accesses mapped memory directly
```

SQLite now uses:
- xFetch()
- memory-mapped pages
- fewer copy operations

SQLite documentation explains mmap allows SQLite to access mapped pages directly using xFetch() instead of xRead(). :contentReference[oaicite:3]{index=3}

---

# Important Clarification About mmap

mmap does NOT:
- load the entire DB into RAM immediately
- bypass the kernel completely

Instead:
- OS lazily loads pages
- virtual memory pages are mapped
- kernel still manages page faults and page cache

mmap primarily reduces:
- extra copying
- syscall overhead
- duplicate buffering

SQLite documentation explicitly mentions that mmap reduces copying between kernel space and user space. :contentReference[oaicite:4]{index=4}

---

# Step 4 — Query Timing (Small Dataset)

Enabled SQLite timer:

```sql
.timer on
```

Query:

```sql
SELECT COUNT(*) FROM titanic;
```

Output:

```text
891
Run Time: real 0.001
user 0.000173
sys  0.000405
```

---

# Observation 3 — Dataset Was Too Small

The dataset:
- fit easily into RAM
- fit inside OS page cache

Meaning:
- benchmark differences were too small
- timings were dominated by cache hits

This demonstrated:
- small datasets are poor benchmarking workloads

---

# Step 5 — Scaling the Dataset

Created larger table:

```sql
CREATE TABLE big_titanic AS
SELECT * FROM titanic;
```

Repeated multiple times:

```sql
INSERT INTO big_titanic
SELECT * FROM big_titanic;
```

This exponentially increased table size.

---

# Final Large Dataset Statistics

| Metric | Value |
|---|---|
| Database Size | ~128 MB |
| Page Count | 32729 |
| Row Count | 1,824,768 |

---

# Observation 4 — Realistic Benchmarking Requires Scale

At 128 MB:
- page traversal became significant
- memory pressure increased
- page cache behavior became meaningful
- syscall overhead became measurable

This transformed the experiment into a realistic storage-engine benchmark.

---

# Step 6 — Benchmark WITHOUT mmap

Disabled mmap:

```sql
PRAGMA mmap_size = 0;
```

Query:

```sql
SELECT COUNT(*) FROM big_titanic;
```

Results:

```text
1824768
Run Time: real 0.084
user 0.009787
sys  0.073762
```

Second run:

```text
1824768
Run Time: real 0.085
user 0.012470
sys  0.072519
```

---

# Observation 5 — Kernel/System Time Dominated

Important observation:

```text
sys time >> user time
```

Meaning:
- SQLite computation itself was cheap
- most time was spent in:
  - kernel operations
  - memory movement
  - page access
  - file interaction
  - page cache management

This experimentally demonstrated:
> database performance is heavily dominated by I/O and memory movement.

---

# Step 7 — Benchmark WITH mmap

Enabled mmap:

```sql
PRAGMA mmap_size = 268435456;
```

Query:

```sql
SELECT COUNT(*) FROM big_titanic;
```

Results:

First run:

```text
1824768
Run Time: real 0.031
user 0.005012
sys  0.026064
```

Second run:

```text
1824768
Run Time: real 0.005
user 0.003845
sys  0.000633
```

---

# Comparison Analysis

| Metric | Without mmap | With mmap |
|---|---|---|
| real time | ~0.084 sec | ~0.005 sec |
| sys time | ~0.073 sec | ~0.0006 sec |
| user time | ~0.009 sec | ~0.003 sec |

---

# Observation 6 — mmap Significantly Reduced Kernel Overhead

With mmap:
- SQLite avoided many extra copy operations
- fewer read() syscalls were needed
- page access became more memory-oriented

The major improvement came from:
- reduced data movement
- reduced kernel overhead
- shared access to OS-managed page cache

NOT from faster SQL computation itself.

SQLite documentation explains mmap improves performance mainly by avoiding unnecessary copying between kernel space and user space. :contentReference[oaicite:5]{index=5}

---

# Observation 7 — Warm Cache Effects

Second mmap run became extremely fast:

```text
real 0.005
```

Reason:
- pages were already mapped
- data already existed in page cache
- most operations became RAM-speed memory access

This demonstrated the importance of:
- page cache
- virtual memory
- caching
- memory locality

in database systems.

---

# Key Technical Learnings

## 1. Databases Operate Around Pages

SQLite internally manages:
- fixed-size pages
- not individual rows

---

## 2. Database Size Includes Internal Structures

Databases store:
- metadata
- B-Trees
- headers
- free space
- schema information

not just raw row data.

---

## 3. Disk I/O Is the Biggest Bottleneck

Most query execution time was spent:
- moving data
- accessing pages
- interacting with kernel memory systems

not computing SQL logic.

---

## 4. mmap Reduces Data Movement

mmap improved performance by:
- reducing copying
- reducing syscall overhead
- leveraging OS virtual memory more efficiently

---

## 5. Warm Caches Dramatically Improve Performance

Repeated queries became significantly faster because:
- data already existed in RAM
- disk access was minimized

---

# Final Conclusion

This experiment demonstrated that modern databases are fundamentally optimized around minimizing disk I/O.

SQLite internally organizes data using:
- pages
- B-Trees
- page cache
- virtual memory interactions

The benchmark showed that:
- SQL computation itself is relatively cheap
- the real bottleneck is moving data between disk, kernel memory, and user space

Enabling mmap significantly reduced kernel overhead by allowing SQLite to access memory-mapped pages directly instead of repeatedly copying pages using traditional read() operations.

The experiment also highlighted the critical role of:
- caching
- virtual memory
- page-oriented storage
- memory locality

in modern database performance engineering.

---

# Commands Used

## SQLite Setup

```bash
sqlite3 lab1.db
```

---

## Import CSV

```sql
.mode csv
.import Titanic-Dataset.csv titanic
```

---

## Table Verification

```sql
.tables
SELECT * FROM titanic LIMIT 5;
```

---

## Page Exploration

```sql
PRAGMA page_size;
PRAGMA page_count;
```

---

## mmap Exploration

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 0;
PRAGMA mmap_size = 1048576;
PRAGMA mmap_size = 268435456;
```

---

## Timing Queries

```sql
.timer on
SELECT COUNT(*) FROM titanic;
SELECT COUNT(*) FROM big_titanic;
```

---

## Scaling Dataset

```sql
CREATE TABLE big_titanic AS
SELECT * FROM titanic;

INSERT INTO big_titanic
SELECT * FROM big_titanic;
```

---

# References

- SQLite Memory-Mapped I/O Documentation  
  [SQLite mmap documentation](https://www.sqlite.org/mmap.html?utm_source=chatgpt.com)

- SQLite File Format Documentation  
  [SQLite File Format Documentation](https://sqlite.org/fileformat.html?utm_source=chatgpt.com)

- SQLite Documentation  
  [SQLite Documentation](https://sqlite.org/docs.html?utm_source=chatgpt.com)

- mmap System Call Overview  
  [mmap System Call Overview](https://en.wikipedia.org/wiki/Mmap?utm_source=chatgpt.com)
