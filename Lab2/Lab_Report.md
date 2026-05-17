# Database Systems Lab Report
## SQLite3 vs PostgreSQL

**Name:** Srujan Gowda



# Objective
The goal of this lab was to practically understand how SQLite3 and PostgreSQL manage storage, memory, and query execution internally. 
My experiments mainly focused on:
- Page / block size
- Page count
- mmap behavior in SQLite
- Query execution timing
- Database architecture and running processes

I performed this assignment using Ubuntu on WSL2.

---

# Part 1 - SQLite3 Exploration

## Database Creation
I started by creating a sample SQLite database named `students.db`.

### Table Used
```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT,
    marks INTEGER
);
```

### Sample Data
```sql
INSERT INTO users(name, email, marks)
VALUES
('Srujan', 'srujan@gmail.com', 91),
('Rahul', 'rahul@gmail.com', 78),
('Ananya', 'ananya@gmail.com', 88),
('Kiran', 'kiran@gmail.com', 67),
('Megha', 'megha@gmail.com', 95);
```

### File Size Observation
Executed in the terminal:
```bash
ls -lh
```

**Output:**
```
-rw-r--r-- 1 srujan srujan 8.0K students.db
```

**Observation:**
I noticed that SQLite stores the complete database inside a single `.db` file. The database size remained very small (8.0K) because I only inserted a few rows. This shows why SQLite is considered lightweight and easy to manage since everything lives in just one file.

### Page Size
```sql
PRAGMA page_size;
```

**Output:**
```
4096
```

**Observation:**
SQLite uses a default page size of 4096 bytes (4KB). Databases don't read individual rows directly from the disk; instead, they read and write in these fixed-size pages. I also realized that this 4KB page size matches the default memory page size used by most operating systems.

### Page Count
```sql
PRAGMA page_count;
```

**Output:**
```
2
```

**Observation:**
The database only used 2 pages internally because my dataset was tiny. 
Approximate internal database size: 4096 bytes × 2 = 8192 bytes (which perfectly matches the ~8KB file size from earlier).

### mmap Experiment

**Checking Current mmap Size:**
```sql
PRAGMA mmap_size;
```
**Output:**
```
0
```
Initially, memory mapped I/O was disabled (set to 0).

**Enabling mmap:**
```sql
PRAGMA mmap_size = 30000000;
```
**Output:**
```
30000000
```

**Observation:**
After enabling mmap, SQLite maps the database file directly into virtual memory. Normally, SQLite makes extra read system calls, but with mmap enabled, the OS can map the pages directly into memory to reduce overhead. For my small database, it didn't feel vastly different since the whole thing already fit easily into memory.

### Query Timing

**Without mmap:**
```bash
time sqlite3 students.db "SELECT * FROM users;"
```
**Output:**
```
real    0m0.003s
user    0m0.000s
sys     0m0.003s
```

**With mmap:**
```bash
time sqlite3 students.db "PRAGMA mmap_size=30000000; SELECT * FROM users;"
```
**Output:**
```
real    0m0.002s
user    0m0.002s
sys     0m0.001s
```

**Observation:**
The query executed a tiny bit faster after I enabled mmap. The difference was very small (0.003s down to 0.002s) because the dataset was tiny, it already fit in memory, and the Linux page cache had probably optimized the repeated reads anyway. This showed me that mmap is likely much more useful for larger, read-heavy workloads.

### SQLite Process Observation
```bash
ps aux | grep sqlite
```

**Observation:**
I didn't see SQLite running as a separate background server process in the terminal. This is because it is an embedded database engine that runs directly inside the application's process. It makes it very portable, but obviously limits concurrency compared to bigger, dedicated database systems.

---

# Part 2 - PostgreSQL Exploration

## Database Creation
Next, I created a PostgreSQL database named `labdb`.

### Table Used
```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    marks INTEGER
);
```

### Sample Data
```sql
INSERT INTO users(name, marks)
VALUES
('Srujan', 91),
('Rahul', 78),
('Ananya', 88),
('Kiran', 67),
('Megha', 95);
```

### Query Output
```sql
SELECT * FROM users;
```

**Output:**
```
 id |  name  | marks
----+--------+-------
  1 | Srujan |    91
  2 | Rahul  |    78
  3 | Ananya |    88
  4 | Kiran  |    67
  5 | Megha  |    95
```

### PostgreSQL Block Size
```sql
SHOW block_size;
```

**Output:**
```
8192
```

**Observation:**
PostgreSQL uses 8192 byte (8KB) blocks by default. Unlike SQLite's 4KB pages, PostgreSQL uses larger blocks because it's built to handle bigger server workloads. Larger block sizes help reduce disk reads during massive table scans.

### PostgreSQL Page Count
```sql
SELECT relpages
FROM pg_class
WHERE relname='users';
```

**Initial Output:**
```
0
```
It showed 0 initially because the statistics weren't updated yet.

**Running ANALYZE:**
```sql
ANALYZE users;
```

**After ANALYZE:**
```sql
SELECT relpages
FROM pg_class
WHERE relname='users';
```

**Updated Output:**
```
1
```

**Observation:**
PostgreSQL stores planner statistics separately, and I had to update them manually using `ANALYZE`. This is quite different from SQLite's simpler metadata approach. PostgreSQL relies heavily on these statistics for query optimization.

### Query Timing
```sql
\timing on
SELECT * FROM users;
```

**Output:**
```
Time: 0.785 ms
```

**Observation:**
The execution time was very fast (under a millisecond) since there were only 5 rows. PostgreSQL internally uses shared memory and query planning even for simple queries like this one.

### PostgreSQL Process Architecture
```bash
ps aux | grep postgres
```

**Processes Observed:**
- checkpointer
- background writer
- walwriter
- autovacuum launcher
- logical replication launcher

**Observation:**
Unlike SQLite, I saw that PostgreSQL runs as a full database server with multiple background processes. Each process handles a specific job like writing WAL logs or cleaning old rows. The `psql` command I used is just a terminal client to talk to the server. This makes PostgreSQL much more powerful for concurrent users.

---

# SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded | Client-Server |
| **Storage** | Single `.db` file | Multiple internal files |
| **Page / Block Size** | 4096 bytes | 8192 bytes |
| **Page Count** | 2 | 1 |
| **mmap Support** | Yes | Uses shared memory internally |
| **Background Processes**| No | Yes |
| **Setup Complexity** | Very simple | More complex |
| **Query Timing** | ~0.002s | ~0.785 ms |
| **Best Use Case** | Local lightweight apps | Multi-user production systems |

# Conclusion
SQLite was super easy to use because it works as an embedded database stored entirely in one file. 
PostgreSQL had a more advanced architecture and server-oriented, with all its background processes and larger storage blocks. 
Testing mmap in SQLite only showed a small performance bump since my database was tiny, but it was a great way to see how it works under the hood. 

## Overall Takeaways

- SQLite is better suited for lightweight applications and local storage because of its simple embedded architecture.
- PostgreSQL is more suitable for scalable multi-user systems due to its server-based design and advanced process management.