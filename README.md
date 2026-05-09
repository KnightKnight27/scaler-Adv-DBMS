# Database Performance Lab: SQLite3 and PostgreSQL Comparison

## Author Details
- **Name:** Mohammed Masihuddin
- **Roll Number:** 24BCS10570

## 1. Investigating SQLite3

### Executed Commands
- Checking database file sizes: 
  ```bash
  ls -lh
  ```
- Retrieving the default page size: 
  ```sql
  PRAGMA page_size;
  ```
- Getting the total number of pages: 
  ```sql
  PRAGMA page_count;
  ```
- Viewing and modifying the memory-map (mmap) limit: 
  ```sql
  PRAGMA mmap_size;
  PRAGMA mmap_size=268435456; -- Example: setting mmap to 256MB
  ```
- Measuring query execution time:
  ```bash
  time sqlite3 sample.db "SELECT * FROM users;"
  ```
- Monitoring system processes for SQLite: 
  ```bash
  ps aux | grep sqlite
  ```

### Key Findings
- **Database Size:** As new records are added, the physical file size (verified using `ls -lh`) increases accordingly.
- **Default Page Size:** By default, SQLite allocates data in chunks of 4096 bytes (4KB) per page.
- **Total Pages:** The `page_count` PRAGMA reliably shows the exact number of pages the database file utilizes for storage.
- **Effects of `mmap_size`:** 
  - Utilizing memory-mapped I/O (mmap) enables SQLite to interact with the database file straight from RAM, bypassing the need to copy data into a standard I/O buffer.
  - Raising the `mmap_size` limit effectively lowers both CPU usage and I/O wait times.
  - During testing with `time SELECT * FROM users;`, turning on and expanding the `mmap_size` yielded a clear improvement in read speeds when measured against traditional I/O methods.

---

## 2. Working with PostgreSQL (PSQL)

### Executed Commands
- Checking the configured block (page) size: 
  ```sql
  SHOW block_size;
  ```
- Determining the number of pages used by a specific table:
  ```sql
  SELECT relpages FROM pg_class WHERE relname = 'users';
  ```
- Enabling execution time tracking in the PSQL prompt:
  ```sql
  \timing on
  SELECT * FROM users;
  ```

### Key Findings
- **Block Size:** PostgreSQL operates with a bulkier default page size of 8192 bytes (8KB).
- **Page Tracking:** In contrast to SQLite's database-wide page counter, PostgreSQL monitors block usage on a per-table or per-index basis. This approach offers a more detailed view of spatial distribution.
- **Speed and Memory:** While queries run very quickly, the underlying memory architecture differs significantly. Instead of a direct `mmap_size` setting, PostgreSQL utilizes an OS-level page cache alongside its own `shared_buffers` mechanism for standard query processing.

---

## 3. Side-by-Side Analysis

| Characteristic | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Standard Page Size** | 4096 bytes (4KB) | 8192 bytes (8KB) |
| **Page Monitoring** | Analyzed at the database level (`PRAGMA page_count`) | Analyzed per relation/table (`pg_class.relpages`) |
| **Execution Speed** | Extremely quick for simple, local read operations. | Built and optimized for heavy, concurrent read/write transactions and advanced queries. |
| **Memory Management** | Easily adjustable with `PRAGMA mmap_size`. Employing mmap drastically improves local read times by eliminating redundant data copying. | Depends heavily on OS caching and its internal `shared_buffers`. Engineered to manage memory for high-volume concurrent access rather than basic file mapping. |

### Final Thoughts
- **SQLite3** is an excellent, lightweight option that interacts directly with a single file. Adjusting the `mmap_size` to permit direct memory access offers substantial I/O performance gains, making it perfect for applications needing a straightforward embedded database.
- **PostgreSQL** is a robust, client-server relational database management system. With a larger 8KB default page size and advanced memory handling capabilities, it is much better equipped to manage complex, highly concurrent, and large-scale workloads, although it demands more initial configuration than SQLite.
