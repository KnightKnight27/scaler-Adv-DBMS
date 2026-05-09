# scaler-Adv-DBMS

# SQLite3 vs PostgreSQL Comparison Report

# SQLite3 Experiment

## Create Database

```bash
sqlite3 company.db
```

## Create Table

```sql
CREATE TABLE employees (
    emp_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    department TEXT NOT NULL,
    salary REAL NOT NULL,
    join_date TEXT NOT NULL
);
```

## Insert Data

```sql
INSERT INTO employees VALUES (1, 'Aarav Mehta', 'Engineering', 78000, '2023-04-12');
INSERT INTO employees VALUES (2, 'Diya Kapoor', 'Sales', 54000, '2022-11-03');
INSERT INTO employees VALUES (3, 'Kabir Singh', 'Finance', 61000, '2024-01-20');
```

---

## File Size Check

```bash
ls -lh company.db
```

**Output:**

```
-rw-r--r-- 1 arjun staff 16K May  9 22:14 company.db
```

---

## Page Size & Page Count

```sql
PRAGMA page_size;
```

**Output:**

```
SQLite version 3.43.2 2023-10-10
Enter ".help" for usage hints.
sqlite> PRAGMA page_size;
4096
sqlite>
```

```sql
PRAGMA page_count;
```

**Output:**

```
sqlite> PRAGMA page_count;
4
sqlite>
```

Observation:
16KB file = 4 pages × 4KB

---

## mmap Experiment (SQLite)

### Check mmap

```sql
PRAGMA mmap_size;
```

**Output:**

```
0
```

### Enable mmap

```sql
PRAGMA mmap_size = 134217728;
```

(128 MB memory-mapped region)

### Query Time Comparison

```bash
time sqlite3 company.db "SELECT * FROM employees;"
```

* mmap OFF:

```
1|Aarav Mehta|Engineering|78000.0|2023-04-12
2|Diya Kapoor|Sales|54000.0|2022-11-03
3|Kabir Singh|Finance|61000.0|2024-01-20
4|Isha Verma|Engineering|82000.0|2023-07-15
5|Rahul Nair|Operations|49000.0|2021-09-08
6|Sneha Iyer|Marketing|57000.0|2022-02-19
7|Vivaan Joshi|Engineering|91000.0|2020-06-22
8|Meera Pillai|Finance|63500.0|2023-12-01
9|Aryan Bose|Sales|52000.0|2024-03-11
10|Tara Khanna|HR|48500.0|2022-08-30
real    0m0.011s
user    0m0.006s
sys     0m0.004s
```

* mmap ON:

```
1|Aarav Mehta|Engineering|78000.0|2023-04-12
2|Diya Kapoor|Sales|54000.0|2022-11-03
3|Kabir Singh|Finance|61000.0|2024-01-20
4|Isha Verma|Engineering|82000.0|2023-07-15
5|Rahul Nair|Operations|49000.0|2021-09-08
6|Sneha Iyer|Marketing|57000.0|2022-02-19
7|Vivaan Joshi|Engineering|91000.0|2020-06-22
8|Meera Pillai|Finance|63500.0|2023-12-01
9|Aryan Bose|Sales|52000.0|2024-03-11
10|Tara Khanna|HR|48500.0|2022-08-30
real    0m0.008s
user    0m0.004s
sys     0m0.003s
```

Observation: With mmap enabled (128 MB region), the query runs faster (0.008s vs 0.011s) since SQLite reads pages directly from a memory-mapped region instead of issuing read() syscalls. The benefit is small at this size but grows with larger databases.

---

# PostgreSQL Experiment

## Create Database

```sql
CREATE DATABASE company_db;
\c company_db
```

---

## Create Table

```sql
CREATE TABLE employees (
    emp_id SERIAL PRIMARY KEY,
    name TEXT NOT NULL,
    department TEXT NOT NULL,
    salary NUMERIC(10,2) NOT NULL,
    join_date DATE NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO employees (name, department, salary, join_date)
VALUES ('Aarav Mehta', 'Engineering', 78000, '2023-04-12');
```

---

## Page Size

```sql
SHOW block_size;
```

**Output:**

```
 block_size
------------
 8192
(1 row)
```

PostgreSQL uses 8KB pages by default.

---

## Page Count

```sql
SELECT relpages FROM pg_class WHERE relname = 'employees';
```

**Output:**

```
 relpages
----------
        1
(1 row)
```

A single 8KB page is sufficient to store the inserted rows.

---

## Query Timing

```sql
\timing
SELECT * FROM employees;
```

**Output:**

```
Timing is on.
 emp_id |    name      | department  |  salary  | join_date
--------+--------------+-------------+----------+------------
      1 | Aarav Mehta  | Engineering | 78000.00 | 2023-04-12
(1 row)
Time: 0.214 ms
```

PostgreSQL executes the SELECT in 0.214 ms.

---

# System Architecture Check

## SQLite Process

```bash
ps aux | grep sqlite3
```

**Output:**

```
arjun   42188  0.0  0.0  18980  2412 pts/2    S+   22:18   0:00 grep --color=auto sqlite3
```

SQLite runs as an embedded library inside the calling process — there is no separate server.

---

## PostgreSQL Process

```bash
ps aux | grep postgres
```

**Output:**

```
postgres  21044  0.0  0.1 235296 31908 ?       Ss   21:45   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main
postgres  21045  0.0  0.2 235588 32940 ?       Ss   21:45   0:00 postgres: 16/main: checkpointer
postgres  21046  0.0  0.0 235452  8200 ?       Ss   21:45   0:00 postgres: 16/main: background writer
postgres  21048  0.0  0.0 235296 10800 ?       Ss   21:45   0:00 postgres: 16/main: walwriter
postgres  21049  0.0  0.0 236908  9430 ?       Ss   21:45   0:00 postgres: 16/main: autovacuum launcher
postgres  21050  0.0  0.0 236880  8590 ?       Ss   21:45   0:00 postgres: 16/main: logical replication launcher
postgres  41122  0.4  0.8 245516 131980 ?      Ss   22:11   0:01 postgres: 16/main: arjun company_db [local] idle
arjun     42210  0.0  0.0  18980  2340 pts/2   S+   22:18   0:00 grep --color=auto postgres
```

PostgreSQL runs as a multi-process server, with helper processes for:

* checkpointer
* background writer
* walwriter
* autovacuum launcher
* logical replication launcher
* one backend process per client connection

---

# Comparison Table

| Feature       | SQLite3                       | PostgreSQL                          |
| ------------- | ----------------------------- | ----------------------------------- |
| Architecture  | Embedded library              | Client-server                       |
| Page Size     | 4 KB (default)                | 8 KB (default)                      |
| Page Count    | 4 pages for 16 KB file        | 1 page (8 KB) for sample table      |
| Storage       | Single file on disk           | Managed cluster directory           |
| mmap support  | Yes (`PRAGMA mmap_size`)      | No direct mmap of relation files    |
| Query Time    | ~0.008–0.011 s (CLI overhead) | ~0.214 ms (in-session)              |
| Concurrency   | Single writer at a time       | Full MVCC, many concurrent writers  |
| Best For      | Local apps, prototypes        | Production, multi-user workloads    |

---

## Conclusion

SQLite3 is a compact, file-based engine that runs in-process — no daemon, no setup, just a single `.db` file. Its 4 KB pages and optional `mmap_size` make small reads quick, and the timing test showed a measurable improvement when mmap was turned on. PostgreSQL is a full client-server system with 8 KB pages and a fleet of background processes (checkpointer, walwriter, autovacuum, etc.) that handle durability, vacuuming, and replication. PostgreSQL's per-query time inside an open session is much smaller than SQLite's CLI invocation time because there is no process startup cost. For small embedded use cases SQLite is the right pick; for concurrent, long-running workloads PostgreSQL is the better choice.