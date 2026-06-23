# SQLite3 vs PostgreSQL: Architectural Comparison, Memory Mapping, and Runtime Characteristics

## 1. Objective & Scope

This assignment analyzes and compares SQLite3 and PostgreSQL from an internal database engine perspective.
The investigation centers on:

- Standard block / page sizes.
- Page allocation and file layouts.
- Memory-mapped I/O (mmap) behaviors.
- Active process models.
- Core design goals of embedded databases versus client-server database management systems.

---

## 2. SQLite3 Hands-on Analysis

### Creating the Table

We created a simple database containing movie data in SQLite3:

```sql
CREATE TABLE movies (
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO movies (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### File Layout

All table structures and data records were stored in a single `.db` file on disk. This layout highlights SQLite's lightweight embedded nature.

Key observations:
- All database entities reside within a single file.
- The file footprint is extremely small for minor tables.
- No separate server storage directories or daemons are required.

### Page Size & Count

To inspect page allocations, we executed:

```sql
PRAGMA page_size;
PRAGMA page_count;
```

Returned metrics:

| Attribute | Measured Value |
|---|---:|
| Page size | 4096 bytes |
| Page count | 2 |

This indicates the database uses two 4 KB pages. For SQLite, the database file size is calculated as:

```text
file_size = page_size × page_count
```

### Memory-Mapped I/O (mmap)

SQLite supports mapping database blocks into memory via the `mmap_size` configuration:

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

Observed responses:

| Param | Configured Value |
|---|---:|
| Initial mmap size | 0 |
| Updated mmap size | 268435456 bytes |

- SQLite defaults to disabling memory-mapped I/O (size = 0).
- Enabling `mmap_size` maps database blocks directly into the application process's virtual memory address space, lowering I/O system call overheads for read tasks.

### Query Latency

A basic table scan was run before and after setting the memory mapping:

```sql
SELECT * FROM users;
```

Observed result:
- Both queries finished almost instantaneously.
- The performance change from enabling `mmap` was negligible because the test dataset fits entirely in standard memory caches anyway.

### Process Architecture

We searched for running database server processes:

```bash
ps aux | grep sqlite
```

Observations:
- No background daemon process was active for SQLite.
- SQLite functions as an in-process library linked directly into the host application.
- This setup simplifies deployment but limits concurrency for write-heavy multi-user setups.

---

## 3. PostgreSQL Hands-on Analysis

### Creating the Table

We configured a PostgreSQL database and created a test table structure:

```sql
CREATE DATABASE testdb;

CREATE TABLE movies (
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO movies (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### File Layout

Unlike SQLite, PostgreSQL does not store all databases in a single user-accessible file. Instead, it segments databases, tables, and system catalogs into dedicated files within a managed data directory. This structure aligns with its client-server model.

### Block Size

We queried the server's default block configuration:

```sql
SHOW block_size;
```

Result:

| Attribute | Measured Value |
|---|---:|
| Block size | 8192 bytes |

PostgreSQL defaults to 8 KB blocks, which is double the size of SQLite's standard page size.

### Page Count and Table Size

PostgreSQL manages page allocation at the table and index levels rather than globally for the entire database file.

```sql
SELECT
    pg_relation_size('users') / current_setting('block_size')::int
    AS approx_page_count;
```

### Query Latency

We ran a full scan:

```sql
SELECT * FROM users;
```

Observations:
- Queries execute quickly.
- Sequential runs show lower latencies due to shared buffer caching.

### Process Architecture

We checked for running daemon processes:

```bash
ps aux | grep postgres
```

Observations:
- PostgreSQL runs as a set of coordinated background daemon processes (postmaster, autovacuum launcher, write-ahead log writer, etc.).
- This multi-process architecture is a major differentiator from SQLite's library-based model.

---

## 4. Structured Comparison Table

| Attribute | SQLite3 | PostgreSQL |
|---|---|---|
| Engine Architecture | Embedded library | Client-server daemon model |
| Disk Storage Layout | Single `.db` file | Segmented relation files inside a data folder |
| Default Page / Block Size | 4096 bytes | 8192 bytes |
| Page Count Tracking | Global `PRAGMA page_count` | Table-specific block calculations |
| Memory Mapping (mmap) | Configurable via `PRAGMA mmap_size` | Managed internally; no direct user mmap setting |
| Runtime Processes | Executes inside the client application | Coordinated background engine processes |
| Deployment & Config | Zero-configuration | Requires server configuration and administration |
| Target Workloads | Local applications, mobile apps, embedded files | Concurrent multi-user production applications |

---

## 5. Summary & Recommendation

SQLite3 is a single-file, zero-configuration embedded engine. It is ideal for local storage, mobile applications, and lightweight read-heavy applications. The engine's page layout is straightforward, and `PRAGMA mmap_size` provides direct memory-mapped read control.

PostgreSQL is a client-server database management system built for high-concurrency environments. It utilizes larger page blocks, background daemons, and storage structures that support advanced concurrency and analytical queries.

Ultimately, SQLite3 is suited for simplicity and low-overhead deployments, while PostgreSQL is the preferred choice for enterprise-scale concurrency and reliability.

---

---