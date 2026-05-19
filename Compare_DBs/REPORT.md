# SQLite3 vs PostgreSQL Comparison Report

# SQLite3 Experiment (doing as instructed in class):

## Create Database

```bash
sqlite3 sample_db
```

## Create Table

```sql
CREATE TABLE clubs (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    location TEXT NOT NULL
);
```

## Insert Data into clubs table

```sql
INSERT INTO clubs (id, name, street_location) VALUES
(1, 'Bangalore Club', 'Residency Road'),
(2, 'Koramangala Club', '80 Feet Road, Koramangala'),
(3, 'Indiranagar Club', '100 Feet Road, Indiranagar'),
(4, 'Whitefield Sports Club', 'ITPL Main Road, Whitefield'),
(5, 'Jayanagar Recreation Club', '11th Main Road, Jayanagar');
```


## File Size Check after creating first table

```bash
ls -lh sample_db
```

**Output:**

```
-rw-r--r-- 1 pratham-onkar-singh pratham-onkar-singh 8.0K May 18 20:26 sample_db
```


---

## Create another Table in same db

```sql
CREATE TABLE cities (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    state TEXT NOT NULL
);
```

## Insert Data into clubs table

```sql
INSERT INTO cities (id, name, state) VALUES
(1, 'Bengaluru', 'Karnataka'),
(2, 'Mumbai', 'Maharashtra'),
(3, 'Chennai', 'Tamil Nadu'),
(4, 'Hyderabad', 'Telangana'),
(5, 'Kolkata', 'West Bengal');
```

## File Size Check after creating second table

```bash
ls -lh sample_db
```

**Output:**

```
-rw-r--r-- 1 pratham-onkar-singh pratham-onkar-singh 12K May 18 20:29 sample_db

```

Observation: 
new pages weren't created for new table instead free space in previous pages was used since page size increased by 4K or only 1 page was created on creation of new table.  

---


## Page Size & Page Count

**Page Size:**
```sql
PRAGMA page_size;
```

**Output:**

```
4096
```

**Page Count:**
```sql
PRAGMA page_count;
```

**Output:**

```
3
```

Observation:
since file is of 12KB it must have 4 pages of size 4KB each.

---

## mmap Experiment (SQLite)

### Enable timer

```sql
.timer on;
```

### Check mmap size

```sql
PRAGMA mmap_size;
```

**Output:**

```
0
```

### Query Time Comparison

```bash
SELECT * FROM clubs;
```

- mmap OFF:

```
1|Bangalore Club|Residency Road
2|Koramangala Club|80 Feet Road, Koramangala
3|Indiranagar Club|100 Feet Road, Indiranagar
4|Whitefield Sports Club|ITPL Main Road, Whitefield
5|Jayanagar Recreation Club|11th Main Road, Jayanagar
Run Time: real 0.000 user 0.000086 sys 0.000026
```

### Enable mmap

```sql
PRAGMA mmap_size = 4194304;
```

- mmap ON:

```
1|Bangalore Club|Residency Road
2|Koramangala Club|80 Feet Road, Koramangala
3|Indiranagar Club|100 Feet Road, Indiranagar
4|Whitefield Sports Club|ITPL Main Road, Whitefield
5|Jayanagar Recreation Club|11th Main Road, Jayanagar
Run Time: real 0.001 user 0.000079 sys 0.000023
```

Observation: 
Mmap enabled with 4MB improves performance slightly (0.000086s vs 0.000079s). Without mmap kernel has to first make system calls to disk to retrieve file and then make a copy to send to user buffer ans also there is context switching between kernel mode and user mode which also creates overhead but by creating a mmap we reduce these steps by creating mappings to files (not loading entire files just a pointer to those files, they are lazily loaded), so now user can directly get those files from a shared memory buffer which is kept by kernel.

---

# PostgreSQL Experiment

## Create Database

```sql
CREATE DATABASE sample_db;
\c sample_db
```

---

## Create Table

```sql
CREATE TABLE clubs (
    id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    street_location VARCHAR(200)
);
```

---

## Insert Data

```sql
INSERT INTO clubs (name, street_location) VALUES
('Bangalore Club', 'Residency Road'),
('Koramangala Club', '80 Feet Road, Koramangala'),
('Indiranagar Club', '100 Feet Road, Indiranagar'),
('Whitefield Sports Club', 'ITPL Main Road, Whitefield'),
('Jayanagar Recreation Club', '11th Main Road, Jayanagar');
```

---

## Page Size

```sql
SELECT current_setting('block_size');
```

**Output:**

```
 current_setting
-----------------
 8192
(1 row)
```

Observation:
PostgreSQL uses 8KB pages, unlike sqlite3 which uses 4KB pages.

---

## Query Timing

```sql
\timing
```
**Output:**
```
Timing is on.
```

**Query**

```sql
SELECT * FROM clubs;
```

**Output:**

```
 id |           name            |      street_location       
----+---------------------------+----------------------------
  1 | Bangalore Club            | Residency Road
  2 | Koramangala Club          | 80 Feet Road, Koramangala
  3 | Indiranagar Club          | 100 Feet Road, Indiranagar
  4 | Whitefield Sports Club    | ITPL Main Road, Whitefield
  5 | Jayanagar Recreation Club | 11th Main Road, Jayanagar
(5 rows)

Time: 0.358 ms
```

Time taken by PostgreSQL to execute same query: 0.358ms.

# System Architecture Check

## SQLite Process

```bash
ps aux | grep sqlite3
```

**Output:**

```
pratham+   23160  0.0  0.0  11964  5400 pts/0    S+   20:23   0:00 sqlite3 sample_db
pratham+   24974  0.0  0.0   9152  2284 pts/1    S+   21:03   0:00 grep --color=auto sqlite3
```

SQLite runs as embedded library (no server process). So it is embedded in the same process as our application/server process. No new process is created.

---

## PostgreSQL Process

```bash
ps aux | grep postgres
```

**Output:**

```
postgres   27644  0.0  0.1 225468 31424 ?        Ss   21:05   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres   27645  0.0  0.1 225732 22856 ?        Ss   21:05   0:00 postgres: 16/main: checkpointer 
postgres   27646  0.0  0.0 225624  7960 ?        Ss   21:05   0:00 postgres: 16/main: background writer 
postgres   27648  0.0  0.0 225468 10416 ?        Ss   21:05   0:00 postgres: 16/main: walwriter 
postgres   27649  0.0  0.0 227076  8828 ?        Ss   21:05   0:00 postgres: 16/main: autovacuum launcher 
postgres   27650  0.0  0.0 227052  8156 ?        Ss   21:05   0:00 postgres: 16/main: logical replication launcher 
root       27956  0.0  0.0  19816  7664 pts/2    S+   21:12   0:00 sudo -u postgres psql
root       27957  0.0  0.0  19816  2660 pts/3    Ss   21:12   0:00 sudo -u postgres psql
postgres   27958  0.0  0.0  26092  9300 pts/3    S+   21:12   0:00 /usr/lib/postgresql/16/bin/psql
postgres   27961  0.0  0.1 228208 19932 ?        Ss   21:13   0:00 postgres: 16/main: postgres sample_db [local] idle
pratham+   28085  0.0  0.0   9152  2292 pts/1    S+   21:17   0:00 grep --color=auto postgres
```

Observation:

PostgreSQL runs as multi-process server:
Along with main PostgreSQL process where the main db cluster is running there are many other processes that are running in background alongside;

1: Checkpointer -> Responsible for periodically flushing the modified data. It basically syncs RAM data with disk to maintain crash safety.
```
postgres: 16/main: checkpointer
```

2: Background writer ->  This process is for smoothening of performance by doing periodical flushing to disk so that there arent any sudden I/O spikes to disk.
```
postgres: 16/main: background writer
```

3: WAL writer -> PostgreSQL first records changes in WAL (Write Ahead Log) before modifying actual tables to provide durability.
```
postgres: 16/main: walwriter
```

4: Autovacuum launcher -> PostgreSQL uses MVCC (Multi-Version Concurrency Control). It cleans up dead rows, update some stats etc.;
```
postgres: 16/main: autovacuum launcher
```

5: Logical replication launcher -> mainly for database replication and streaming to maintain availability of DB.
```
postgres: 16/main: logical replication launcher
```

---

# Comparison Table

| Feature | SQLite | PostgreSQL |
|---|---|---|
| Architecture | Embedded, serverless | Client-server DB |
| Storage | Single `.db` file | Managed data directory |
| Concurrency | Limited | High concurrent access |
| Setup | Zero configuration | Requires DB server setup |
| Best For | Local apps, prototypes | Production backends |
| Background Processes | None | WAL writer, autovacuum, etc. |
---

## Conclusion
PostgreSQL and SQLite are both relational databases, but they are designed for very different use cases. SQLite is an embedded, serverless database engine that runs directly inside an application process and stores the entire database in a single file. There is no separate database server, background workers, or network layer when you run 'sqlite3 sample_db', process directly reads and writes the database file. This makes SQLite extremely lightweight, portable, and easy to set up.

PostgreSQL, on the other hand, is a full client-server database system built for concurrent users, reliability, and large-scale workloads. It runs as a dedicated database server with multiple background processes such as the WAL writer, checkpointer, autovacuum launcher, and one backend process per client connection. Instead of applications directly modifying database files, clients communicate with the PostgreSQL server, which manages transactions, concurrency, caching, crash recovery, replication, permissions, and query optimization. This architecture is more complex and resource-heavy than SQLite, but it allows PostgreSQL to handle many simultaneous users, advanced SQL features, large datasets, and production grade workloads reliably.
