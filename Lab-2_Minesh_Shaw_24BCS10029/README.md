# SQLite3 vs PostgreSQL Comparison
## SQLite3 Experiments
### Commands Used

```bash
ls -lh
```
```bash
sqlite3 test.db
```
```sql
PRAGMA page_size;
```
```sql
PRAGMA page_count;
```
```sql
PRAGMA mmap_size;
```
```sql
PRAGMA mmap_size = 268435456;
```
```bash
time sqlite3 test.db "PRAGMA mmap_size=0; SELECT * FROM users;"
```
```bash
time sqlite3 test.db "PRAGMA mmap_size=268435456; SELECT * FROM users;"
```
```bash
ps aux | grep sqlite
```

---

### Observations

- #### Database File Size
    Database file size: 2.0 MB
    SQLite stores the complete database inside a single file (test.db).

- #### Page Size
    SQLite page size: 4096 bytes (4 KB)

- #### Page Count
    Total pages: 488
- #### mmap_size
    - Default mmap size was 0.
    - mmap size was set to 256 MB during query execution.
    - mmap configuration did not persist across sessions and reset back to 0 after reopening SQLite.
    - mmap configuration appeared to be connection-specific in this environment.

- #### Query Performance
    - Query Timing Without mmap

        ```bash
        real 0m0.630s
        user 0m0.072s
        sys 0m0.427s
        ```
    - Query Timing With mmap (256 MB)

        ```bash
        real 0m0.508s
        user 0m0.066s
        sys 0m0.396s
        ```

- #### Performance Observation
    - Query execution with mmap enabled was slightly faster.
    - mmap reduced system call overhead slightly.
    - Difference was noticeable but small because the database size was relatively small.

- #### SQLite Process Observation

```bash
minesh 6811 0.0 0.0 6672 4352 pts/0 S+ 14:46 0:00 sqlite3 test.db
```

- #### Process Observation
    - SQLite runs as a single lightweight process.
    - It does not require a separate database server.
    - Memory usage and CPU usage were very low.

## PostgreSQL Experiments

### Commands Used

```bash
sudo service postgresql start
```
```bash
sudo -u postgres psql
```
```sql
SHOW block_size;
```
```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```
```sql
EXPLAIN ANALYZE SELECT * FROM users;
```
```bash
ps aux | grep postgres
```
### Observations
- #### Block Size

    ```sql
    block_size
    8192
    ```
    PostgreSQL block size: 8192 bytes (8 KB)

- #### Page Count

    ```sql
    relpages
    636
    ```
    PostgreSQL table page count: 636

- #### Query Execution Time

    ```sql
    QUERY PLAN
    Seq Scan on users (cost=0.00..1636.00 rows=100000 width=17) (actual time=0.004..5.079 rows=100000 loops=1)
    Planning Time: 0.142 ms
    Execution Time: 7.985 ms
    ```

- #### Query Performance Observation
    - PostgreSQL executed the sequential scan efficiently.
    - Query planning and execution were handled separately.
    - PostgreSQL uses an advanced query planner and optimizer.

- #### PostgreSQL Processes

```bash
postgres 9894 0.0 0.3 220284 31104 ? Ss 15:15 0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres 9895 0.0 0.2 220416 19396 ? Ss 15:15 0:00 postgres: 16/main: checkpointer
postgres 9896 0.0 0.1 220436 7876 ? Ss 15:15 0:00 postgres: 16/main: background writer
postgres 9898 0.0 0.1 220284 10308 ? Ss 15:15 0:00 postgres: 16/main: walwriter
postgres 9899 0.0 0.1 221880 8900 ? Ss 15:15 0:00 postgres: 16/main: autovacuum launcher
postgres 9900 0.0 0.1 221864 8132 ? Ss 15:15 0:00 postgres: 16/main: logical replication launcher
```

- #### Process Observation
    - PostgreSQL uses multiple background processes.
    - Dedicated processes exist for:
        - WAL writing
        - Checkpointing
        - Background writing
        - Autovacuum
        - Replication

    - PostgreSQL follows a client-server architecture unlike SQLite.

## Comparison Analysis
| Feature |	SQLite3 |	PostgreSQL |
|:---|:---|:---|
| Architecture	| Embedded database |	Client-server database | 
| Storage |	Single file |	Multiple internal files|
| Page Size |	4096 bytes |	8192 bytes |
| Page Count |	488	| 636|
| mmap Support |	Direct mmap support |	Uses internal shared buffers|
| Query Timing |	~0.5-0.6 seconds |	~8 ms execution time|
| Processes |	Single lightweight process |	Multiple server processes|
| Setup Complexity |	Very simple |	More complex|
| Concurrency Support |	Limited |	Strong concurrency support|
| Resource Usage |	Very low |	Higher than SQLite|


## Conclusion

SQLite3 is lightweight, simple, and easy to use because it operates as an embedded database inside a single file. It uses very few system resources and supports memory mapping through mmap, which slightly improved query performance in the experiment.

PostgreSQL is a full client-server relational database system with multiple background processes and advanced internal components. It provides better concurrency, query planning, and scalability compared to SQLite, but requires more system resources and setup complexity.

### From the experiments:

- SQLite was easier to configure and inspect.
- PostgreSQL had a more advanced architecture.
- mmap slightly improved SQLite performance.
- PostgreSQL demonstrated efficient query execution and process management.