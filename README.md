# Lab 2: SQLite3 vs PostgreSQL Exploration

**Role Number:** 24BCS10267
**Name:** Ujjawal Prabhat

---

## Objective

The purpose of this lab was to study and compare **SQLite3** and **PostgreSQL** through database experiments concentrating on:

* Page size
* Page count
* Query execution time
* `mmap` behavior in SQLite
* Differences in storage and architecture

---

## 1. SQLite3 Exploration

### Installation

```bash
sudo apt install sqlite3

# Verify installed version
sqlite3 --version
```

### Database Setup

Create the database along with a sample table:

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
('Ujjawal', 'Engineering', 75000),
('Rahul', 'Design', 60000),
('Aman', 'Marketing', 50000);
```

### Checking File Size

```bash
ls -lh
```

**Observation:** The `sample.db` file size is 8 KB. SQLite stores the entire database within a single file.

### Page Size

```sql
PRAGMA page_size;
```

**Output:** 4096

**Observation:** SQLite’s default page size is 4096 bytes (4 KB).

### Page Count

```sql
PRAGMA page_count;
```

**Output:** 2

**Observation:** The database currently uses 2 pages.

### `mmap` Experiment

```sql
-- Display current mmap size
PRAGMA mmap_size;

-- Activate mmap (256 MB)
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

**Observation:** Activating `mmap` slightly enhanced query performance. Since the database was small, the improvement was minor, but it helped reduce additional copying between kernel space and user space.

### SQLite Process Observation

```bash
ps aux | grep sqlite
```

**Observation:** SQLite does not operate as an independent server process. Instead, it runs within the application process because it is an embedded database system.

---

## 2. PostgreSQL Exploration

### Installation

```bash
sudo apt install postgresql postgresql-contrib

# Launch PostgreSQL service
sudo systemctl start postgresql

# Verify version
psql --version
```

### Database Setup

Access the PostgreSQL shell and create the database:

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
('Ujjawal', 'Engineering', 75000),
('Rahul', 'Design', 60000),
('Aman', 'Marketing', 50000);
```

### PostgreSQL Page Size

```sql
SHOW block_size;
```

**Output:** 8192

**Observation:** PostgreSQL’s default block size is 8192 bytes (8 KB).

### PostgreSQL Page Count

```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```

**Output:** 1

**Observation:** PostgreSQL maintains metadata about table pages within its system catalogs.

### Query Performance

```sql
EXPLAIN ANALYZE SELECT * FROM users;
```

**Output:** Execution Time: 0.150 ms

**Observation:** Query execution is highly efficient. PostgreSQL performs query planning and optimization before execution.

### PostgreSQL Process Observation

```bash
ps aux | grep postgres
```

**Observation:** Several PostgreSQL processes were running simultaneously because PostgreSQL uses a client-server architecture.

---

## 3. SQLite3 vs PostgreSQL Comparison

### Feature Comparison Matrix

| Feature           | SQLite3                        | PostgreSQL                     |
| ----------------- | ------------------------------ | ------------------------------ |
| Architecture      | Embedded                       | Client-Server                  |
| Default Page Size | 4 KB                           | 8 KB                           |
| Storage Type      | Single File                    | Multiple Files                 |
| Server Required   | No                             | Yes                            |
| `mmap` Support    | Yes                            | Internal Buffering             |
| Query Performance | Fast for small applications    | Better for large-scale systems |
| Concurrency       | Limited                        | High                           |
| Best Use Case     | Lightweight/local applications | Enterprise-level applications  |

### `mmap` Impact Comparison (SQLite)

| Condition       | Query Time |
| --------------- | ---------- |
| `mmap` disabled | ~0.012s    |
| `mmap` enabled  | ~0.008s    |

**Observation:** Enabling `mmap` slightly lowered query execution time by minimizing file I/O overhead.

---

## Analysis

### SQLite3

* Extremely lightweight and simple to configure.
* Best suited for local applications and embedded environments.
* Stores the complete database in a single file.
* Performs faster for smaller workloads because there is no server communication overhead.

### PostgreSQL

* Powerful and highly scalable.
* Provides excellent concurrency support.
* Uses advanced query optimization and buffer management techniques.
* Well suited for production systems involving multiple users and complex transactions.

---

## Conclusion

From the experiments carried out in this lab:

* SQLite3 was found to be simple and lightweight, functioning without the need for a dedicated server process.
* PostgreSQL demonstrated a strong client-server architecture designed for scalability and concurrency.
* Enabling SQLite `mmap` optimization resulted in a noticeable improvement in read performance by reducing I/O overhead.
* PostgreSQL required more system resources because of its dedicated server processes, which is justified for larger-scale environments.

**Final Verdict:** SQLite is best suited for small-scale projects and local data storage, while PostgreSQL is more appropriate for enterprise applications that demand high concurrency and advanced data management capabilities.
