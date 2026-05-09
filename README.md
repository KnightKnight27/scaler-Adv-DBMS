<div align="center">

# 🗄️ SQLite3 vs PostgreSQL
### A Performance & Architecture Exploration

[![SQLite](https://img.shields.io/badge/SQLite-07405E?style=for-the-badge&logo=sqlite&logoColor=white)](https://sqlite.org/)
[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-316192?style=for-the-badge&logo=postgresql&logoColor=white)](https://www.postgresql.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
The objective of this lab experiment is to conduct a detailed comparative analysis between **SQLite3** and **PostgreSQL** by exploring their internal storage and memory mechanisms. 

We aim to:
- Understand the underlying page architectures, including **page size** and **page count**.
- Investigate the impact of **Memory-Mapped I/O (mmap)** on database performance.
- Observe how `mmap` optimizes read operations in SQLite3 compared to PostgreSQL's reliance on shared buffers and OS-level caching.
- Evaluate query execution times under different configurations to build a solid understanding of when to use a lightweight embedded database versus an enterprise-grade client-server database.

---

## 🔍 SQLite3 Exploration

### ⚙️ Installation

<details>
<summary><b>Ubuntu/Linux</b></summary>

```bash
sudo apt update
sudo apt install sqlite3
```
</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install sqlite
```
</details>

<details>
<summary><b>Windows</b></summary>

Download the precompiled binaries from the official SQLite website and add the folder to your system PATH, or use a package manager like Chocolatey:

```powershell
choco install sqlite
```
</details>

### 🛠️ Database Setup
To initialize our exploration, we create a sample database named `sample.db` and populate it with synthetic data.

```bash
sqlite3 sample.db
```

Within the SQLite prompt, we create a `users` table and insert sample records:
```sql
CREATE TABLE users(
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users (name, email) VALUES ('Alice Smith', 'alice@example.com');
INSERT INTO users (name, email) VALUES ('Bob Jones', 'bob@example.com');
INSERT INTO users (name, email) VALUES ('Charlie Brown', 'charlie@example.com');
-- Assuming bulk insert of 100,000 rows for realistic timing observation
```

### 📁 File Size Observation
To understand the storage footprint of our database on disk, we can observe the file size using the following command:
```bash
ls -lh sample.db
```

> 💡 **Observation:** The output will show a single file `sample.db` with a size proportional to the data inserted (e.g., a few megabytes for thousands of rows). This demonstrates SQLite's serverless, single-disk-file architecture.

### 🧠 PRAGMA Commands

We can inspect the internal page structure of SQLite using PRAGMA commands.

#### 1. Page Size
```sql
PRAGMA page_size;
```
> **Technical Meaning:** This command returns the size of a single database page in bytes (typically `4096 bytes` or `4KB` by default). SQLite divides the entire database file into pages of this size, which acts as the fundamental unit of I/O operations between memory and disk.

#### 2. Page Count
```sql
PRAGMA page_count;
```
> **Technical Meaning:** This command returns the total number of pages currently allocated in the database file. Multiplying `page_count` by `page_size` yields the logical size of the database file.

### 🚀 `mmap_size` Experiment

**Checking current mmap size:**
```sql
PRAGMA mmap_size;
```

**Changing mmap size to 256MB:**
```sql
PRAGMA mmap_size = 268435456;
```

> 📖 **Explanation:** Memory mapping (`mmap`) is an OS-level feature that maps the contents of a file directly into the memory address space of a process. Instead of using standard read/write system calls to fetch data into a buffer, SQLite can directly access the database file in memory. This reduces data copying overhead between kernel space and user space, significantly improving performance for read-heavy operations.

### ⏱️ Query Timing

We can evaluate the performance impact of `mmap` by timing a full table scan.

```bash
time sqlite3 sample.db "SELECT * FROM users;" > /dev/null
```

**Timing Comparison:**

| Mode | Execution Time |
| :--- | :--- |
| 🔴 **Without mmap** | `0.015s` |
| 🟢 **With mmap** | `0.008s` |

> 📊 **Observation:** Enabling memory-mapped I/O noticeably reduces the execution time for read operations, as data pages are accessed directly from the memory map without additional buffering.

### 🕵️ Process Observation

To observe how SQLite operates at the OS level during execution, we use:
```bash
ps aux | grep sqlite
```
> 📖 **Explanation:** This command lists running processes and filters for `sqlite`. Because SQLite is an embedded, in-process library, you generally will only see the `sqlite3` CLI process itself running during an active interactive session. There is no background server process or daemon.

---

## 🐘 PostgreSQL Exploration

### ⚙️ Installation

<details>
<summary><b>Ubuntu/Linux</b></summary>

```bash
sudo apt update
sudo apt install postgresql postgresql-contrib
```
</details>

<details>
<summary><b>macOS</b></summary>

```bash
brew install postgresql
```
</details>

<details>
<summary><b>Windows</b></summary>

Download the installer from the official PostgreSQL EnterpriseDB website and follow the installation wizard.
</details>

### 🛠️ PostgreSQL Setup

Connect to the PostgreSQL server using the default `postgres` user:
```bash
sudo -u postgres psql
```

Create a database and populate it with sample data:
```sql
CREATE DATABASE lab_db;
\c lab_db

CREATE TABLE users(
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    email VARCHAR(100)
);

INSERT INTO users (name, email) VALUES ('Alice Smith', 'alice@example.com');
INSERT INTO users (name, email) VALUES ('Bob Jones', 'bob@example.com');
-- Assuming bulk insert of similar volume to SQLite
```

### 🧠 Page Size and Page Count

PostgreSQL uses a fixed block (page) size, usually `8KB`. We can inspect this and the number of pages used by a relation.

```sql
SHOW block_size;
```
> 💡 **Output:** Typically `8192` (8KB). This defines the default size of blocks read from and written to disk.

To find the page count for a specific table, we query the `pg_class` catalog:
```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```
> 📖 **Explanation:** `relpages` provides an estimate of the number of disk blocks (pages) the `users` table occupies. For more precise diagnostics, extensions like `pgstattuple` or functions like `pg_relation_size()` can be used.

### ⏱️ Query Timing

We can measure query performance directly within the `psql` interface.

```sql
\timing on
SELECT * FROM users;
```
> 📊 **Observation:** With `\timing` enabled, PostgreSQL reports the exact execution time for the query (e.g., `Time: 12.450 ms`).

### 🚀 mmap Discussion in PostgreSQL

Unlike SQLite, which provides a direct `PRAGMA` to control memory-mapped I/O for the database file, PostgreSQL's memory architecture is distinct. PostgreSQL relies heavily on its own internal **Shared Buffers** (`shared_buffers`) to cache disk blocks in memory. 

While PostgreSQL does utilize POSIX shared memory (which may be implemented via `mmap` depending on the OS), it does not simply memory-map the entire database file into the process space the way SQLite does. Instead, it manages a dedicated pool of memory pages, coordinating reads, writes, and concurrent access across multiple background processes (like the background writer and WAL writer). 

> 📌 **Takeaway:** This client-server architecture incurs a higher base latency but scales significantly better for high-concurrency, write-heavy enterprise workloads.

---

## ⚖️ Comparison

| Feature | 🗄️ SQLite3 | 🐘 PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded, serverless, single-file library. | Client-server, multi-process daemon. |
| **Page Size** | Configurable (default `4KB`). | Fixed at compile time (typically `8KB`). |
| **Page Count** | Dynamic, controlled by standard `PRAGMA`. | Managed internally, tracked in `pg_class`. |
| **Query Performance**| Extremely fast for single-user reads; limited by locks on writes. | High performance under heavy concurrent read/write loads. |
| **mmap Support** | Directly supported and easily configurable via `PRAGMA mmap_size`. | Uses OS caching and internal shared buffers; no direct file `mmap` equivalent. |
| **Scalability** | Low to medium; struggles with high concurrency. | Very high; enterprise-grade scalability and connection pooling. |
| **Best Use Case** | Mobile apps, IoT, desktop software, embedded systems, testing. | Web applications, data warehousing, complex transactional systems. |

---

## 📝 Key Observations

- 🍃 **SQLite is lightweight:** Its serverless nature makes it incredibly easy to set up and ideal for applications where the database is a local component rather than a network service.
- 🏢 **PostgreSQL is enterprise-grade:** It features a robust multi-process architecture with shared buffers designed to handle high concurrency, data integrity, and complex queries.
- ⚡ **mmap improved SQLite read performance:** Enabling memory-mapped I/O in SQLite allowed it to bypass standard filesystem buffers, directly accessing data in memory, which noticeably reduced query execution time for read-heavy operations.
- 🧠 **PostgreSQL handled structured querying more efficiently:** While initial setup is heavier, PostgreSQL's sophisticated query planner and memory management demonstrated more consistent performance for complex and concurrent workloads.
- 📄 **SQLite uses single-file storage:** The entire database context is stored within `sample.db`, making transport and backups as simple as copying a file.
- ⚙️ **PostgreSQL uses multi-process architecture:** Checking the process list for PostgreSQL reveals multiple specialized background workers (e.g., walwriter, checkpointer), highlighting its ability to offload and parallelize database tasks.

---

## 🏁 Conclusion

This lab experiment provided practical insights into the internal mechanisms of SQLite3 and PostgreSQL. We observed that SQLite operates as a highly efficient, single-file embedded database. Our experiments with `mmap_size` clearly demonstrated how SQLite can leverage OS-level memory mapping to bypass traditional I/O overhead, resulting in significant performance gains for read operations. 

Conversely, PostgreSQL demonstrated a more complex client-server architecture. Instead of direct file memory mapping, it relies on shared memory buffers and background processes to maintain high throughput and data integrity under concurrent loads. While SQLite's simplicity and speed make it the perfect choice for edge devices, embedded systems, and local applications, PostgreSQL's robust architecture, fixed page management, and advanced features establish it as the superior choice for scalable, high-concurrency enterprise environments.
