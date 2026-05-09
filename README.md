# Lab Tasks Comparison Report: SQLite3 vs PostgreSQL

## 1. Observations

### SQLite3
*   **File Sizes**: Using `ls -lh` on the database file revealed the initial file size on disk.
*   **Page Size**: Retrieved using the `PRAGMA page_size;` command.
*   **Page Count**: Retrieved using the `PRAGMA page_count;` command.
*   **mmap_size Impact**: Investigated by altering `PRAGMA mmap_size`. Enabling/increasing the `mmap_size` allows SQLite to memory-map the database file, which can significantly improve read performance by avoiding data copies between kernel space and user space.

### PostgreSQL (PSQL)
*   **Page Size**: Retrieved using the `SHOW block_size;` command.
*   **Page Count**: Calculated by querying the system catalogs to find the relation size and dividing by the block size.

## 2. Commands Used

### SQLite3 Commands
*   **Access Database**: `sqlite3 sample.db`
*   **Check File Size**: `ls -lh sample.db`
*   **Get Page Size**: `PRAGMA page_size;`
*   **Get Page Count**: `PRAGMA page_count;`
*   **Set mmap Size**: `PRAGMA mmap_size = <size_in_bytes>;` (e.g., `PRAGMA mmap_size = 268435456;` for 256MB)
*   **Time Query Execution**: 
    *   Inside SQLite CLI: `.timer on` followed by `SELECT * FROM users;`
    *   From Terminal: `time sqlite3 sample.db "SELECT * FROM users;"`
*   **Monitor Process**: `ps aux | grep sqlite`

### PostgreSQL Commands
*   **Access Database**: `psql -U username -d databasename`
*   **Get Page (Block) Size**: `SHOW block_size;`
*   **Get Page Count for a Table**: `SELECT pg_relation_size('users') / current_setting('block_size')::int AS page_count;`
*   **Time Query Execution**: `EXPLAIN ANALYZE SELECT * FROM users;` or `\timing` followed by the query.

## 3. Comparison Analysis

### Page Size & Page Count
| Feature               | SQLite3                                              | PostgreSQL                                            |
| **Default Page Size** | Typically 4KB (4096 bytes), but configurable.        |Typically 8KB (8192 bytes), compiled into the server. |
| **Page Count**        | Determined by `database_size / page_size`.           | Determined per relation: `relation_size / block_size`.          |

### Query Performance
*   **SQLite3**: Extremely fast for local, read-heavy operations. The execution time of a simple `SELECT * FROM users;` is generally minimal since it's an embedded database (no network overhead).
*   **PostgreSQL**: Uses a client-server architecture. For a simple `SELECT *` on a small table, the network/connection overhead might make it slightly slower than SQLite3. However, PostgreSQL scales much better with large datasets, complex queries, and high concurrent user access.

### `mmap` Impact (SQLite3)
*   **Without mmap**: SQLite uses standard `read()` and `write()` system calls. Data must be copied from the operating system's file system cache into SQLite's user-space heap memory before it can be used.
*   **With mmap**: Setting `PRAGMA mmap_size` enables memory-mapped I/O. The database file is mapped directly into the process's address space. This avoids the memory copy overhead, allowing SQLite to read data directly via pointers. This generally leads to a noticeable decrease in execution time for `SELECT` queries, especially on large tables that fit within the mapped memory region.
