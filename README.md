# Advanced DBMS Lab Assignment

**Name:** T. Abdul Kalam Azad
**Role Number:** 24BCS10053

## Observations and Comparison Analysis

### 1. Database Exploration: SQLite3 vs PostgreSQL

This report outlines the differences between SQLite3 and PostgreSQL in terms of internal storage organization, page handling, and query performance based on experimental observations.

#### Page Size
* **SQLite3:** 
  * The default page size is typically 4096 bytes (4 KB).
  * **Command used:** `PRAGMA page_size;`
* **PostgreSQL:**
  * The default page size (block size) is 8192 bytes (8 KB).
  * **Command used:** `SHOW block_size;`

#### Page Count
* **SQLite3:**
  * For a sample table of 100,000 records, the page count was found to be 1492.
  * Total size = 1492 pages × 4096 bytes/page ≈ 6.1 MB.
  * **Command used:** `PRAGMA page_count;`
* **PostgreSQL:**
  * Page count varies depending on how data is physically arranged, but it is generally calculated by dividing the relation size by the block size.
  * **Command used:** `SELECT pg_relation_size('users') / current_setting('block_size')::numeric;`

#### Memory Mapping (mmap) Impact on Query Performance
Memory-mapped I/O (`mmap`) allows the database engine to map files directly into memory space, often resulting in faster read operations.

* **SQLite3 Experiment:**
  * **Command to set mmap:** `PRAGMA mmap_size=268435456;` (Sets limit to 256 MB)
  * **Command to disable mmap:** `PRAGMA mmap_size=0;`
  * **Execution Time without mmap:** ~1343 milliseconds
  * **Execution Time with mmap:** ~1139 milliseconds
  * **Observation:** Enabling mmap in SQLite3 resulted in noticeably faster SELECT query execution times because the database avoids standard system calls (like `read()`) and accesses data directly through memory pointers.

* **PostgreSQL Behavior:**
  * PostgreSQL does not use `mmap` in the same way SQLite does. Instead, it relies on a large internal **shared_buffers** cache combined with the Operating System's file system cache. 
  * Because PostgreSQL is a client-server architecture designed for heavy concurrency, its buffer management is highly optimized for multi-user scenarios rather than direct memory mapping.

### 2. General Differences
* **Architecture:** SQLite3 is an embedded database (the database engine runs within the application process), whereas PostgreSQL is a client-server database (the database runs as a separate background process).
* **Process Observation:** 
  * **Command:** `ps aux | grep sqlite` (Linux/Mac) or `Get-Process | Where-Object {$_.Name -match "sqlite"}` (Windows).
  * In SQLite3, the query runs inside the invoking application's process. For PostgreSQL, query execution is handled by background worker processes managed by the main PostgreSQL server.
