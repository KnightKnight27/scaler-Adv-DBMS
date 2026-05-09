# Database Management Systems - Lab Task

**Name:** Piyush Pawan Kumar  
**Role Number:** 24bcs10296  

---

## 1. SQLite3 Exploration (Java Implementation)

For this lab, the database generation and query execution were implemented in **Java using JDBC**.

### Commands and Setup

```bash
# 1. Compile the Java program
javac DatabaseExperiment.java

# 2. Run the Java program to generate DB and measure query execution time
java -cp ".:sqlite-jdbc.jar:slf4j-api.jar:slf4j-simple.jar" DatabaseExperiment

# 3. Check file size using shell
ls -lh sample_java.db

# 4. View currently running SQLite processes
ps aux | grep sqlite
```

### Database Experiment Code (Java snippet)
```java
long selectStartTime = System.currentTimeMillis();
try (Statement selectStmt = conn.createStatement();
     ResultSet rs = selectStmt.executeQuery("SELECT * FROM users")) {
    int count = 0;
    while (rs.next()) { count++; }
    long selectEndTime = System.currentTimeMillis();
    System.out.println("Fetched " + count + " records in " + (selectEndTime - selectStartTime) + " ms.");
}
```

### Finding Page Size and Page Count via CLI
```bash
sqlite3 sample_java.db "PRAGMA page_size;"
sqlite3 sample_java.db "PRAGMA page_count;"
```

### Observations
- **File Size:** `43M` (for 1,000,000 records inserted via Java JDBC)
- **Page Size:** `4096` bytes (4 KB)
- **Page Count:** `10893`
- **Execution Time (Java JDBC Select All):** `139 ms` (0.139s)
- **mmap Impact:** Changing `mmap_size` in SQLite maps the DB file directly to process memory, improving I/O performance. In Java, this means the native driver accesses the mapped memory faster, slightly reducing the overhead of transferring data from the OS buffer to the Java application.

---

## 2. PostgreSQL (PSQL) Setup & Exploration

### Commands Used
```sql
-- 1. Create database and table
CREATE DATABASE sample_db;
\c sample_db
CREATE TABLE users (id SERIAL PRIMARY KEY, username TEXT, email TEXT, age INT);

-- 2. Find Page Size
SELECT current_setting('block_size');

-- 3. Find Page Count
SELECT relpages FROM pg_class WHERE relname = 'users';

-- 4. Measure query execution time
\timing on
SELECT * FROM users;
```

### Observations
- **File Size:** ~`52M` (PostgreSQL has higher per-row overhead due to MVCC metadata like xmin/xmax).
- **Page Size (Block Size):** `8192` bytes (8 KB)
- **Page Count:** `6656` (approximate for 1M records)
- **Execution Time:** ~`0.315s`
- **Caching Mechanism:** PostgreSQL heavily relies on `shared_buffers` instead of relying completely on the OS page cache or `mmap`. Once the data is cached in `shared_buffers`, subsequent queries perform significantly faster.

---

## 3. Comparison Report

| Feature | SQLite3 (JDBC) | PostgreSQL |
| :--- | :--- | :--- |
| **Architecture** | Embedded / Serverless | Client-Server RDBMS |
| **Default Page Size** | `4096` bytes (4 KB) | `8192` bytes (8 KB) |
| **Page Count (1M rows)** | `10893` | `6656` |
| **File / Storage Size** | `43 MB` | `52 MB` |
| **Query Execution Time** | `0.139s` (Java JDBC execution) | `0.315s` (CLI execution) |
| **Memory Mapping (mmap)** | Supported (`PRAGMA mmap_size`). Speeds up reads by mapping file directly to memory. | Does not natively use `mmap` for data files; relies on `shared_buffers` and OS page cache. |

### Analysis
1. **Page Size & Count**: SQLite uses a smaller default page size (4 KB) compared to PostgreSQL (8 KB). Consequently, for a similar dataset, SQLite has a higher page count. PostgreSQL's larger page size is optimized for extensive disk I/O typical in enterprise workloads.
2. **Storage Footprint**: SQLite stores data more compactly. PostgreSQL requires additional hidden columns (`xmin`, `xmax`, etc.) per row for MVCC (Multi-Version Concurrency Control), resulting in a larger file size overall.
3. **Query Performance**: The Java JDBC implementation for SQLite was very fast (139ms) because SQLite operates within the same JVM process, resulting in zero network latency and no IPC (Inter-Process Communication) overhead. PostgreSQL introduces slight overhead due to its robust client-server architecture, but will vastly outperform SQLite in concurrent connections and complex joins.
4. **mmap Impact**: SQLite benefits directly from `mmap` because it allows the process to read the disk file as if it were in RAM. PostgreSQL handles its own shared memory architecture (`shared_buffers`) to manage cache independently of the OS's `mmap`, providing fine-grained control over caching at the cost of memory overhead.
