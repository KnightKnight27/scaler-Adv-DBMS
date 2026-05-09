# 🗄️ Database Storage Internals – Lab Assignment

---

## Part 1: SQLite3 Exploration

### Installation

Install SQLite3 on your system before proceeding.

---

### Step 1: Set Up a Sample Database

Use any sample database (e.g., a `users` table with dummy records).

Check the database file size using:

```bash
ls -lh
```

**Observe:** Note the `.db` file size on disk.

---

### Step 2: PRAGMA Commands

Connect to your SQLite3 database and run the following PRAGMA commands:

```sql
-- Get page size (in bytes)
PRAGMA page_size;

-- Get total number of pages
PRAGMA page_count;
```

**Observe:** Record the page size and page count values.

---

### Step 3: Experiment with `mmap_size`

```sql
-- Check current mmap_size
PRAGMA mmap_size;

-- Set mmap_size to a non-zero value (e.g., 30MB)
PRAGMA mmap_size = 30000000;

-- Disable mmap
PRAGMA mmap_size = 0;
```

**Observe:** Note any behavioral differences when mmap is enabled vs disabled.

---

### Step 4: Time Your Queries

Run a timed SELECT query to benchmark performance:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Run this **twice** — once with mmap enabled and once with mmap disabled — and record the execution times.

---

### Step 5: Monitor SQLite3 Process

While a query is running, open another terminal and run:

```bash
ps aux | grep sqlite
```

**Observe:** Note the memory and CPU usage of the SQLite3 process.

---

## Part 2: PostgreSQL (PSQL) Setup

### Installation

Install PostgreSQL and start the service before proceeding.

---

### Experiments

Perform the same experiments as SQLite3:

**Page Size:**

```sql
SHOW block_size;
```

**Page Count (for a specific table):**

```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```

**Query Execution Time:**

```sql
-- Enable timing
\timing

-- Run query
SELECT * FROM users;
```

**Observe:** Record page size, page count, and query execution time.
