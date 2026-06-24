# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** 24BCS10274  
**Name:** Viraj Bhanage

---

## Objective

The objective of this lab was to explore and compare **SQLite3** and **PostgreSQL** by conducting database experiments focusing on:

- Page size
- Page count
- Query execution time
- `mmap` behavior in SQLite
- Storage and architecture differences

---

## 1. SQLite3 Exploration

### Installation

```bash
sudo apt install sqlite3

# Check installed version
sqlite3 --version
```

### Database Setup

Create the database and a sample table:

```bash
sqlite3 sample.db
```

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    department TEXT,
    salary INTEGER
);

INSERT INTO users(name, department, salary)
VALUES
('Viraj', 'Engineering', 75000),
('Rahul', 'Design', 60000),
('Aman', 'Marketing', 50000);
```

### Checking File Size

```bash
ls -lh
```

**Observation:** File size of `sample.db` is 8 KB. SQLite stores the complete database inside a single file.

### Page Size

```sql
PRAGMA page_size;
```

**Output:** 4096

**Observation:** SQLite uses a default page size of 4096 bytes (4 KB).

### Page Count

```sql
PRAGMA page_count;
```

**Output:** 2

**Observation:** The database currently occupies 2 pages.

### `mmap` Experiment

```sql
-- Check current mmap size
PRAGMA mmap_size;

-- Enable mmap (256 MB)
PRAGMA mmap_size = 268435456;
```

### Query Timing (Without vs. With `mmap`)

Without `mmap`:

```bash
time sqlite3 sample.db "PRAGMA mmap_size=0; SELECT * FROM users;"
```

Result:

```text
real    0m0.012s
user    0m0.004s
sys     0m0.005s
```

With `mmap`:

```bash
time sqlite3 sample.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
```

Result:

```text
real    0m0.008s
user    0m0.003s
sys     0m0.003s
```

**Observation:** Enabling `mmap` slightly improved query performance. The improvement was small due to the small database size, but it reduces extra copying between kernel space and user space.

### SQLite Process Observation

```bash
ps aux | grep sqlite
```

**Observation:** SQLite does not run as a separate server process. It runs directly inside the application process because it is an embedded database engine.

## 2. PostgreSQL Exploration

### Installation

```bash
sudo apt install postgresql postgresql-contrib

# Start PostgreSQL service
sudo systemctl start postgresql

# Check version
psql --version
```

### Database Setup

Open the PostgreSQL shell and set up the database:

```bash
sudo -u postgres psql
```

```sql
CREATE DATABASE labdb;
\c labdb

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    department TEXT,
    salary INTEGER
);

INSERT INTO users(name, department, salary)
VALUES
('Viraj', 'Engineering', 75000),
('Rahul', 'Design', 60000),
('Aman', 'Marketing', 50000);
```

### PostgreSQL Page Size

```sql
SHOW block_size;
```

**Output:** 8192

**Observation:** PostgreSQL uses a default block size of 8192 bytes (8 KB).

### PostgreSQL Page Count

```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```

**Output:** 1

**Observation:** PostgreSQL internally stores metadata about table pages in system catalogs.

### Query Performance

```sql
EXPLAIN ANALYZE SELECT * FROM users;
```

**Output:** Execution Time: 0.150 ms

**Observation:** Query execution is highly optimized. PostgreSQL includes query planning and optimization before execution.

### PostgreSQL Process Observation

```bash
ps aux | grep postgres
```

**Observation:** Multiple PostgreSQL processes were running concurrently because PostgreSQL follows a robust client-server architecture.

## 3. SQLite3 vs PostgreSQL Comparison

### Feature Comparison Matrix

| Feature | SQLite3 | PostgreSQL |
| --- | --- | --- |
| Architecture | Embedded | Client-Server |
| Default Page Size | 4 KB | 8 KB |
| Storage Type | Single File | Multiple Files |
| Server Required | No | Yes |
| `mmap` Support | Yes | Internal Buffering |
| Query Performance | Fast for small apps | Better for large systems |
| Concurrency | Limited | High |
| Best Use Case | Lightweight/local apps | Enterprise applications |

### `mmap` Impact Comparison (SQLite)

| Condition | Query Time |
| --- | --- |
| `mmap` disabled | ~0.012s |
| `mmap` enabled | ~0.008s |

**Observation:** `mmap` reduced query execution time slightly by reducing file I/O overhead.

## Analysis

### SQLite3

- Very lightweight and easy to set up.
- Ideal for local applications and embedded systems.
- The entire database exists as a single file.
- Faster for simple workloads because there is no server communication overhead.

### PostgreSQL

- Highly powerful and scalable.
- Superior concurrency support.
- Utilizes advanced query planning and buffer management.
- Suitable for production systems with many users and complex transactions.

## Conclusion

Based on the experiments conducted in this lab:

- SQLite3 proved to be simpler and highly lightweight, operating entirely without a dedicated server process.
- PostgreSQL demonstrated a robust client-server architecture built for scalability and concurrency.
- Enabling SQLite `mmap` optimization showed a measurable improvement in read performance by minimizing I/O overhead.
- PostgreSQL consumed more system resources due to its dedicated server processes, justifying its use in larger environments.

**Final Verdict:** SQLite is the ideal choice for small projects and local data storage, whereas PostgreSQL is the superior choice for enterprise-level applications requiring high concurrency and complex data management.

