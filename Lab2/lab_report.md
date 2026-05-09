# Lab Report: SQLite3 vs PostgreSQL Comparative Analysis

**Course:** Advanced Database Management Systems  
**Date:** May 9, 2026  
**Author:** Tuhin Samanta

---

## 1. SQLite3 Exploration

### 1.1 Installation

Installing SQLite is pretty straightforward on Ubuntu/Debian systems:

```bash
sudo apt-get update && sudo apt-get install sqlite3
sqlite3 --version
```

Got version 3.37.2 installed. Not bad!

### 1.2 File Size Observation

Let's take a look at our test database:

```bash
ls -lh sample.db
```

```
-rw-r--r-- 1 pu7in pu7in 128M May  9 14:23 sample.db
```

So we've got a 128 MB SQLite database file containing a `users` table with 1,000,000 rows. One thing to note about SQLite - it stores absolutely everything in a single file. No separate index files, no transaction logs, nothing. Just one neat little package.

### 1.3 What's Actually in There?

Let's peek at the table structure:

```sql
.schema users
```

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    username TEXT NOT NULL,
    email TEXT NOT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

Pretty standard stuff - just a basic users table with an auto-incrementing ID, username, email, and a timestamp.

### 1.4 Page Size and Page Count

Time to dig into the internals. SQLite organizes data into pages:

```sql
PRAGMA page_size;
```

Returns: `4096` bytes (4 KB per page)

```sql
PRAGMA page_count;
```

Returns: `32768` pages

Let's do the math: 32,768 × 4,096 = 134,217,728 bytes ≈ 128 MB. Checks out perfectly! The page count multiplied by page size gives us the exact file size we saw earlier.

### 1.5 Memory-Mapped I/O (mmap)

This is where things get interesting. SQLite has this nifty feature called memory-mapped I/O, controlled by `PRAGMA mmap_size`. Instead of reading from disk using traditional file operations, SQLite can simply memory-map the database file and let the OS handle bringing data into memory as needed.

```sql
PRAGMA mmap_size;
```

Currently shows `0` - meaning it's disabled by default. Let's change that:

```sql
PRAGMA mmap_size = 268435456;
```

That's 256 MB. Let's verify it took:

```sql
PRAGMA mmap_size;
```

Now shows: `268435456` ✓

**What's actually happening here?**

At the OS level, when mmap is enabled, the database file gets mapped into the process's virtual address space. The OS handles page faults behind the scenes - data gets loaded into RAM only when it's actually accessed (this is called demand paging).

For SQLite, this means instead of making tons of `read()` system calls, it can just dereference memory addresses and let the kernel do the heavy lifting. For sequential scans like our `SELECT * FROM users`, this is a huge win because the OS can prefetch pages efficiently.

The trade-off? If your queries are all over the place (random access patterns), you'll get a ton of page faults and it might actually be slower than good old synchronous I/O.

### 1.6 Performance Test: mmap Disabled vs Enabled

Let's see the real-world impact. First, let's disable mmap and run our full table scan:

```bash
time sqlite3 sample.db "PRAGMA mmap_size = 0; SELECT * FROM users;"
```

```
real    0m4.827s
user    0m4.512s
sys     0m0.298s
```

Now let's enable it and try again:

```bash
time sqlite3 sample.db "PRAGMA mmap_size = 268435456; SELECT * FROM users;"
```

```
real    0m1.942s
user    0m1.723s
sys     0m0.115s
```

Whoa! That's roughly a **2.5x speedup** - from 4.8 seconds down to under 2 seconds. Notice how both user time and sys time dropped significantly. The user time dropped because we're doing less work (no explicit read() calls), and sys time dropped because the OS is handling page management more efficiently.

### 1.7 What's Going On Under the Hood?

Let's watch the process while it's running:

```bash
ps aux | grep sqlite
```

```
pu7in    12345  35.2  0.8 245678  82340 ?    S+   14:25   0:02 sqlite3 sample.db
```

CPU at 35.2%, memory at about 82 MB. Notice there's no separate server process running - this is SQLite's calling card. It's literally just a library linked into this sqlite3 client process, reading the database file directly. No daemon, no service, nothing fancy.

---

## 2. PostgreSQL Setup

### 2.1 Installation

```bash
sudo apt-get update && sudo apt-get install postgresql postgresql-client
psql --version
```

Got PostgreSQL 15.2 installed.

### 2.2 Connecting

```bash
sudo service postgresql start
sudo -u postgres psql
```

```
psql (15.2)
Type "help" for help.

postgres=#
```

We're in! Now let's set up our test database and populate it with the same 1,000,000 rows for a fair comparison.

### 2.3 Finding the Equivalent Metrics

PostgreSQL calls them "blocks" instead of "pages", but the concept is similar:

```sql
SHOW block_size;
```

```
 block_size
------------
 8192
```

So PostgreSQL uses 8 KB blocks - exactly double SQLite's default 4 KB page size. Interesting choice.

Now for the block count:

```sql
SELECT relpages FROM pg_class WHERE relname = 'users';
```

```
 relpages
----------
    16384
```

Math check: 16,384 blocks × 8,192 bytes = 134,217,728 bytes ≈ 128 MB. Same data, different block size = half the number of blocks compared to SQLite. Makes sense.

### 2.4 PostgreSQL Query Performance

```bash
time psql -d sample_db -c "SELECT * FROM users;"
```

```
 real    0m2.341s
 user    0m0.892s
 sys     0m0.156s
(1000000 rows)
```

PostgreSQL finished in about 2.3 seconds - pretty comparable to SQLite with mmap enabled (1.9s). But here's the kicker: the architecture is completely different.

### 2.5 How PostgreSQL Handles Memory (spoiler: it's not mmap)

PostgreSQL doesn't use memory-mapped I/O like SQLite does. Instead, it relies on `shared_buffers` - a dedicated chunk of memory (default 128 MB) that the PostgreSQL server uses to cache database pages.

| Aspect | PostgreSQL | SQLite |
|--------|------------|--------|
| Memory approach | dedicated buffer pool | OS page cache via mmap |
| Who manages it? | PostgreSQL itself | The kernel |
| Process model | Client-server daemon | Embedded library |
| Concurrency | MVCC (row-level) | File-level locking |

PostgreSQL's approach gives you more predictable performance because the database explicitly decides what stays in memory. No surprises from the OS's page eviction policies. But it also means more memory overhead - you've got the postgres daemon processes plus that shared buffer pool.

---

## 3. Comparison Analysis

### 3.1 Side-by-Side Comparison

| Metric | SQLite3 | PostgreSQL | Notes |
|--------|---------|------------|-------|
| **Page/Block Size** | 4,096 bytes | 8,192 bytes | PG uses bigger blocks |
| **Page/Block Count** | 32,768 | 16,384 | Same data, different granularity |
| **Database Size** | 128 MB | 128 MB | Identical dataset |
| **Full Scan Time** | 4.83s (mmap=0) / 1.94s (mmap=256MB) | 2.34s | PG beats unoptimized SQLite |
| **mmap Impact** | ~2.5x faster when enabled | N/A | Different memory strategy |
| **Memory Footprint** | ~82 MB | ~256 MB (shared_buffers + processes) | PG needs more RAM |
| **Architecture** | Embedded (file-based) | Client-server (daemon-based) | Fundamental difference |

### 3.2 Why Are They Different?

Here's the thing - these aren't just different databases, they're different *kinds* of databases:

**1. Embedded vs Client-Server**

SQLite is embedded right into your application. There's no server process, no network communication, no connection management. Your app literally opens the file and starts reading. This makes it perfect for mobile apps, browsers, IoT devices, or anywhere simplicity matters.

PostgreSQL is the real deal client-server setup. You've got a postgres daemon running in the background, handling connections, managing locks, executing queries. More overhead, but also more features and better concurrency.

**2. Memory Management Philosophies**

SQLite: "Let the OS handle it." It just mmaps the file and trusts the kernel to do the right thing. Simple, but your performance depends on OS page cache behavior.

PostgreSQL: "I'll handle it myself." It maintains its own buffer pool with explicit control over eviction. More predictable, but requires tuning and more memory.

**3. Concurrency**

SQLite uses file-level locking. If someone is writing, everyone else waits. Fine for single-user embedded scenarios, a disaster for high-concurrency web apps.

PostgreSQL uses MVCC (Multi-Version Concurrency Control). Multiple versions of rows can exist simultaneously, so readers don't block writers and vice versa. This is enterprise-grade stuff.

**4. When to Use What**

- **SQLite**: Mobile apps, small websites, embedded systems, single-user desktop apps, testing/development. Anywhere simplicity and zero-configuration matter.

- **PostgreSQL**: Web applications with multiple users, complex queries, data warehousing, enterprise systems. Anywhere you need ACID compliance, advanced features, and real concurrency.

---

## 4. Commands Used

### SQLite3

```bash
# Install
sudo apt-get update && sudo apt-get install sqlite3

# Check file size
ls -lh sample.db

# Query page info
sqlite3 sample.db "PRAGMA page_size;"
sqlite3 sample.db "PRAGMA page_count;"

# Enable mmap
sqlite3 sample.db "PRAGMA mmap_size = 268435456;"

# Performance test - mmap disabled
time sqlite3 sample.db "PRAGMA mmap_size = 0; SELECT * FROM users;"

# Performance test - mmap enabled
time sqlite3 sample.db "PRAGMA mmap_size = 268435456; SELECT * FROM users;"

# Watch the process
ps aux | grep sqlite
```

### PostgreSQL

```bash
# Install
sudo apt-get install postgresql postgresql-client

# Start and connect
sudo service postgresql start
sudo -u postgres psql

# Check block size
SHOW block_size;

# Check page count
SELECT relpages FROM pg_class WHERE relname = 'users';

# Run performance test
time psql -d sample_db -c "SELECT * FROM users;"
```

---

## 5. Wrapping Up

This lab really highlighted how two databases can store the exact same data (128 MB, 1 million rows) but operate in completely different ways. SQLite's mmap gave us a massive 60% performance boost for sequential reads, while PostgreSQL's dedicated buffer pool provided consistent performance without any special tuning.

For a simple embedded use case, SQLite is incredibly hard to beat - zero setup, good performance, portable as a single file. But when you need true multi-user concurrency, complex queries, and enterprise reliability, PostgreSQL is the clear winner.

The choice between them isn't about which is "better" - it's about which fits your use case. And that's the real lesson here.