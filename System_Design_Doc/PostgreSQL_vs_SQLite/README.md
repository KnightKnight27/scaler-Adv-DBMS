# PostgreSQL vs SQLite Architecture Comparison

**Author:** Abhiroop Sistu

**Roll Number:** 24BCS10287

I compared two SQL databases that made opposite design choices. PostgreSQL is a client-server database designed for many users, while SQLite is a lightweight embedded library that runs inside a single application. Both are excellent databases, but they solve different problems.

---

## 1. Problem Background

### PostgreSQL

PostgreSQL originated from the University of California, Berkeley (1986) and was designed as a full-featured multi-user database server. Its goals include:

- Supporting many concurrent users
- Strong transactional correctness
- Rich SQL functionality
- Long-running server deployments

### SQLite

SQLite was released in 2000 with a very different goal: eliminate the need for a database server.

Instead of running as a separate process, SQLite is linked directly into an application and stores everything in a single file.

### Quick Comparison

| Feature | PostgreSQL | SQLite |
|----------|------------|---------|
| Type | Client-Server | Embedded Library |
| Database Storage | Multiple files managed by server | Single database file |
| Designed For | Many concurrent users | One application |
| Concurrency | Many readers and writers | Mostly one writer |
| Network Support | Yes (TCP/IP) | No |
| Deployment | Requires server process | No server required |

---

## 2. Architecture Overview

```text
PostgreSQL (Client-Server)

 clients -> network
      |
  Postmaster
      |
 forks one backend process per client
      |
 shared memory (shared_buffers)
      |
 data files + WAL


SQLite (Embedded)

 Your Application
        |
    Function Call
        |
   SQLite Library
 (Parser + VM + B-Tree + Pager)
        |
     File I/O
        |
     database.db
```

### PostgreSQL

- A postmaster process accepts client connections.
- One backend process is created per client.
- Shared memory stores the buffer cache and lock structures.
- Background processes handle WAL, checkpoints, and autovacuum.

### SQLite

- No server process exists.
- SQL executes through direct function calls.
- Queries compile into bytecode executed by a small virtual machine.
- Database operations access a local file directly.

The key difference is that PostgreSQL communicates through a network, while SQLite operates entirely inside the application's process.

---

## 3. Internal Design

### Storage

#### PostgreSQL

- Tables stored as heaps.
- Rows can exist anywhere within the table.
- Uses 8 KB pages.
- Tables, indexes, and WAL are stored separately.

#### SQLite

- Entire database stored in one file.
- Uses 4 KB pages by default.
- Tables are implemented as B-Trees.
- Rows are stored in rowid order.

```text
PostgreSQL:
Heap File + Separate Indexes

SQLite:
B-Tree Table (Clustered by rowid)
```

---

### Indexes

Both systems primarily use B-Trees.

#### PostgreSQL

Indexes store:

```text
Key -> TID (Physical Row Location)
```

Additional index types:

- Hash
- GiST
- GIN
- BRIN

#### SQLite

Tables themselves are B-Trees.

For an integer primary key:

```text
rowid -> row
```

No additional index is required.

---

### Concurrency

#### PostgreSQL

Uses MVCC.

Each row stores:

- xmin (creating transaction)
- xmax (deleting transaction)

An UPDATE creates a new row version instead of overwriting the old one.

Benefits:

- Readers do not block writers
- Writers do not block readers
- High concurrency

#### SQLite

Uses database-level locking.

```text
Many Readers
      +
One Writer
```

Only one writer can modify the database at a time.

WAL mode improves concurrency by allowing readers to continue while a writer is active.

---

### Durability

#### PostgreSQL

Uses Write-Ahead Logging (WAL).

```text
WAL Record
     |
Flush WAL
     |
Write Data Page Later
```

Recovery replays WAL after a crash.

#### SQLite

Supports:

- Rollback Journal
- WAL Mode

In WAL mode SQLite creates:

```text
database.db
database.db-wal
database.db-shm
```

I verified these files were created during testing.

The database file header also contains:

```text
SQLite format 3
```

---

## 4. Design Trade-Offs

### PostgreSQL

**Advantages**

- Supports many concurrent users
- Strong consistency guarantees
- Sophisticated query planner
- Advanced indexing options

**Costs**

- Requires a running server
- Network communication overhead
- More operational complexity

---

### SQLite

**Advantages**

- No server setup
- Single portable database file
- Extremely lightweight
- Very fast local access

**Costs**

- One writer at a time
- No network access
- Limited scalability for multi-user workloads

---

### MVCC vs File Locking

#### PostgreSQL

```text
New Version Per Update
```

Advantages:

- Excellent concurrency

Cost:

- Dead rows accumulate
- VACUUM required

#### SQLite

```text
Update In Place
```

Advantages:

- Simpler storage management

Cost:

- Writer locking reduces concurrency

---

### Indexes Are Not Free

Indexes improve reads but consume storage and increase write costs.

In my experiment, adding one index increased database size by roughly 50%.

---

## 5. Experiments / Observations

**Environment:** SQLite 3.51.0

Test table:

```sql
CREATE TABLE accounts(
    id INTEGER PRIMARY KEY,
    name TEXT,
    balance REAL,
    city TEXT
);
```

Rows inserted:

```text
50,000
```

---

### Query Planner Behavior

#### Primary Key Lookup

```sql
WHERE id = 42345
```

Plan:

```text
SEARCH accounts USING INTEGER PRIMARY KEY (rowid=?)
```

Direct lookup through the table B-Tree.

---

#### Unindexed Filter

```sql
WHERE city = 'city_7'
```

Plan:

```text
SCAN accounts
```

Full table scan.

---

#### Indexed Filter

```sql
CREATE INDEX idx_city
ON accounts(city);
```

Query:

```sql
WHERE city = 'city_7'
```

Plan:

```text
SEARCH accounts USING INDEX idx_city (city=?)
```

The planner now uses the index B-Tree.

---

### Timing Results

| Query Type | Time |
|------------|-------|
| Full Scan | ~0.004 s |
| Indexed Lookup | ~0.000 s |

Even with only 50,000 rows, the indexed lookup was noticeably faster.

---

### File Size Impact

| State | Size |
|---------|---------|
| Before Index | 1.52 MB |
| After Index | 2.29 MB |

Increase:

```text
~50%
```

This demonstrates that indexes trade storage space for faster query execution.

---

### File Header Verification

Inspecting the database file showed:

```text
SQLite format 3
```

confirming the expected SQLite file format.

---

## 6. Key Learnings

- The biggest difference is the process model:
  - PostgreSQL uses a database server.
  - SQLite runs as a library inside an application.

- SQLite is ideal for:
  - Mobile applications
  - Desktop software
  - Edge devices
  - Embedded systems

- PostgreSQL is ideal for:
  - Web applications
  - Multi-user systems
  - High-concurrency workloads

- MVCC is the foundation of PostgreSQL's concurrency model.

- VACUUM is the maintenance cost of MVCC.

- SQLite trades concurrency for simplicity.

- Indexes improve performance but increase storage usage.

- Query planners make these trade-offs visible through:
  - SCAN (read everything)
  - SEARCH (use an index)

---

## References

1. PostgreSQL Documentation (Internals and Storage)
2. SQLite Documentation (Architecture, File Format, WAL)
3. Experiments performed locally using SQLite 3.51.0