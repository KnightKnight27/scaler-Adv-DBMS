
# SQLite3 vs PostgreSQL Comparison Report


## Create Database

```bash
sqlite3 lab-db
```

## Create Table

```sql
CREATE TABLE studentslist (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

## Insert Data

```sql
INSERT INTO studentslist VALUES (1, "Tanush", 20, "C-");
```

---

## File Size Check

```bash
ls -lh lab-db
```

**Output:**

```
-rw-r--r-- 1 tanush tanush 12K May  9 18:46 lab-db
```

---

## Page Size & Page Count

```sql
PRAGMA page_size;
```

**Output:**

```
SQLite version 3.45.1 2024-01-30 16:01:20
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
3
sqlite> 

```

Observation:
12KB file = 3 pages × 4KB

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
PRAGMA mmap_size = 268435456;
```

### Query Time Comparison

```bash
time sqlite3 lab-db "SELECT * FROM studentslist;"
```

* mmap OFF:

```
1|Tanush|19|C-
2|Liam|19|C-
3|Lee|19|C-
4|Priya|20|A
5|Arjun|21|B
6|Ananya|20|C-
7|Rohan|21|A
8|Neha|20|B
9|Vikram|21|C-
10|Shreya|20|A
11|Aditya|21|B
12|Pooja|20|C-
13|Rishab|21|A

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

* mmap ON:

```
1|Tanush|19|C-
2|Eam|19|C-
3|Eam1|19|C-
4|Priya|20|A
5|Arjun|21|B
6|Ananya|20|C-
7|Rohan|21|A
8|Neha|20|B
9|Vikram|21|C-
10|Shreya|20|A
11|Aditya|21|B
12|Pooja|20|C-
13|Rishab|21|A

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

Observation: Mmap enabled with 256MB improves performance slightly (0.006s vs 0.007s). mmap ON is slightly faster due to memory mapping of the database file.

---

# PostgreSQL Experiment



## Create Database

```sql
CREATE DATABASE lab-db;
\c lab-db
```

---

## Create Table

```sql
CREATE TABLE studentslist (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO studentslist VALUES (1, 'Tanush Shoor', 20, 'A+');
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

PostgreSQL uses 8KB pages

---

## Query Timing

```sql
\timing
SELECT * FROM studentslist;
```

**Output:**

```
Timing is on.
 roll_no |    fullname    | age | grade 
---------+----------------+-----+-------
       1 | Tanush Shoor   |  19 | A+
(1 row)

Time: 0.185 ms
```

PostgreSQL query execution time: 0.185 ms (very fast for single row)

# System Architecture Check

## SQLite Process

```bash
ps aux | grep sqlite3
```

**Output:**

```
Tanush   37129  0.0  0.0  18980  2344 pts/3    S+   18:42   0:00 grep --color=auto sqlite3
```

SQLite runs as embedded library (no server process)

---

## PostgreSQL Process

```bash
ps aux | grep postgres
```

**Output:**

```
postgres   20324  0.0  0.1 235296 31808 ?        SNs  18:03   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres   20325  0.0  0.2 235588 32936 ?        SNs  18:03   0:00 postgres: 16/main: checkpointer 
postgres   20326  0.0  0.0 235452  8180 ?        SNs  18:03   0:00 postgres: 16/main: background writer 
postgres   20328  0.0  0.0 235296 10792 ?        SNs  18:03   0:00 postgres: 16/main: walwriter 
postgres   20329  0.0  0.0 236908  9420 ?        SNs  18:03   0:00 postgres: 16/main: autovacuum launcher 
postgres   20330  0.0  0.0 236880  8588 ?        SNs  18:03   0:00 postgres: 16/main: logical replication launcher 
root       33234  0.0  0.0  29576  7916 pts/0    S<+  18:33   0:00 sudo -u postgres psql
root       33235  0.0  0.0  29576  2644 pts/1    S<s  18:33   0:00 sudo -u postgres psql
postgres   33236  0.0  0.0  35920  9656 pts/1    S<+  18:33   0:00 /usr/lib/postgresql/16/bin/psql
postgres   33237  0.5  0.8 245516 132624 ?       SNs  18:33   0:02 postgres: 16/main: postgres postgres [local] idle
tanush   37153  0.0  0.0  18980  2340 pts/3    S+   18:42   0:00 grep --color=auto postgres
```

PostgreSQL runs as multi-process server:

* checkpointer
* walwriter
* autovacuum
* background writer
* logical replication launcher

---

# Comparison Table

| Feature     | SQLite3              | PostgreSQL                       |
| ----------- | ---------------------| ---------------------------------|
| Type        | Library-based        | Server Architecture              |
| Page Size   | 4KB                  | 8KB                              |
| Storage     | File-based           | System-managed                   |
| mmap        | Available            | Unavailable                      |
| Performance | Fast for small data  | Optimized for large workloads    |
| Concurrency | Limited              | Full support                     |
| Page Count  | 3 pages (12KB total) | Variable based on data           |

---

## Conclusion

SQLite3 is lightweight and easy to use for small applications with a single file and no server. It uses 4KB pages and mmap improves performance slightly. PostgreSQL is a powerful server-based database with 8KB pages, better for large applications with multiple users. SQLite3 is best for local projects, while PostgreSQL is better for production systems.

