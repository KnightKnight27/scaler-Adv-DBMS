# SQLite3 vs PostgreSQL: Architectural and Storage Comparison

## 1. Goal & Objectives

This report analyzes and contrasts the internal engine mechanics of SQLite3 and PostgreSQL. The evaluation covers:
- Default storage block/page sizes
- Page allocation and physical storage layout
- Memory-mapped I/O (mmap) behaviors
- Operating system process footprint
- Design tradeoffs (embedded vs. client-server models)

---

## 2. SQLite3 Analysis

### Schema & Data Setup

We created a simple database containing a table named `films`:
2
```sql
CREATE TABLE films (
    id INTEGER PRIMARY KEY,
    name TEXT
);

INSERT INTO films (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### Storage Characteristics

SQLite compiles the entire database into a single `.db` file on disk. This single-file structure highlights SQLite's lightweight, zero-configuration design:
- All database objects reside in one local file.
- The file overhead is minimal for small datasets.
- No database server processes are involved in managing access.

### Page Metrics

We retrieved the database page allocation parameters:

```sql
PRAGMA page_size;
PRAGMA page_count;
```

**Measured values:**

| Parameter | Current Value |
| :--- | :--- |
| Page Size | 4096 bytes |
| Page Count | 2 |

The total size of the database file equals `page_size * page_count` (8 KB), with the data organized into two 4 KB pages.

### Memory-Mapped I/O (mmap)

SQLite allows memory-mapped access via the `mmap_size` setting:

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

**Mmap sizes configured:**

| Configuration State | Memory Map Size |
| :--- | :--- |
| Default `mmap_size` | 0 bytes (Disabled) |
| Updated `mmap_size` | 268,435,456 bytes (256 MB) |

By default, memory mapping is disabled (`0`). Activating it allows SQLite to map the database file directly into the application process's virtual memory space, decreasing read I/O call overhead.

### Query Latency

Executing a query over the table:

```sql
SELECT * FROM films;
```

Observations:
- The response was instantaneous in both standard and mmap mode.
- Because the dataset is tiny, mapping the pages to virtual memory did not provide a noticeable speedup.

### Process footprints

Checking for SQLite processes:

```bash
ps aux | grep sqlite
```

Key takeaway:
- No background daemon is active for SQLite.
- It runs as an in-process library, which makes deployment simple but limits complex multi-user writing concurrency.

---

## 3. PostgreSQL Analysis

### Schema & Data Setup

We set up a separate database cluster and established the same table structure:

```sql
CREATE DATABASE testdb;

CREATE TABLE films (
    id SERIAL PRIMARY KEY,
    name TEXT
);

INSERT INTO films (name)
VALUES ('Godfather'), ('Moneyball'), ('The Big Short');
```

### Storage Characteristics

Unlike SQLite's single-file approach, PostgreSQL distributes relations across several system files in a designated data directory (typically `base/`). This reflects its server-centric architecture.

### Block Size

To verify the default database page block size, we ran:

```sql
SHOW block_size;
```

**Measured value:**

| Property | Value |
| :--- | :--- |
| Default block size | 8192 bytes (8 KB) |

PostgreSQL operates on 8 KB blocks, which is double SQLite's default page size of 4 KB.

### Page Count and Relation Sizes

Rather than a global count, PostgreSQL calculates page usage per relation:

```sql
SELECT
    pg_relation_size('films') / current_setting('block_size')::int
    AS page_count;
```

### Query Latency

```sql
SELECT * FROM films;
```

Observations:
- The execution time was negligible.
- Successive runs benefited from PostgreSQL's shared buffer cache.

### Process footprints

To inspect PostgreSQL processes, we ran:

```bash
ps aux | grep postgres
```

Observations:
- Spawns several helper background processes (e.g., autovacuum launcher, logical replication launcher, background writer, checkpointer, and walwriter).
- This multi-process server layout is designed to handle high-concurrency environments.

---

## 4. Feature Matrix Comparison

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **System Architecture** | In-process embedded library | Client-server multi-process DBMS |
| **Physical Storage** | Single database file | Multi-file database directory |
| **Page / Block Size** | 4096 bytes (default) | 8192 bytes (default) |
| **Determining Pages** | Global `PRAGMA page_count` | Relation-based size checks |
| **Memory Mapping** | Explicitly via `PRAGMA mmap_size` | Relies on OS page cache & shared buffers |
| **Execution Footprint** | Inside application process | Multiple background daemons |
| **Configuration** | Serverless, zero configuration | Requires admin setup and setup tuning |
| **Primary Use Cases** | Mobile apps, local caches, prototyping | Production databases, high concurrency |

---

## 5. Summary Findings

**SQLite3** is lightweight, zero-maintenance, and easy to deploy. Its single-file storage format and simple page layout are optimized for low-concurrency, local application configurations. The user can adjust `mmap_size` directly to speed up reads via memory mapping.

**PostgreSQL** is an enterprise-grade client-server DBMS. It uses larger page blocks (8 KB), complex multi-file layouts, and a multi-process architecture. While it requires configuration and administration, it is built to manage massive scale, concurrency, and reliability.

---