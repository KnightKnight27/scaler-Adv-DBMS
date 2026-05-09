# SQLite3 vs PostgreSQL Comparison Report

# SQLite3 Experiment

## Create Database

```bash
sqlite3 library-db
```

## Create Table

```sql
CREATE TABLE books (
    book_id INT PRIMARY KEY,
    title TEXT NOT NULL,
    author TEXT NOT NULL,
    category TEXT NOT NULL
);
```

## Insert Data

```sql
INSERT INTO books VALUES (1, "Database Systems", "Narendra", "Education");
```

---

## File Size Check

```bash
ls -lh library-db
```

**Output:**

```bash
-rw-r--r-- 1 narendra narendra 12K May  9 18:46 library-db
```

---

## Page Size & Page Count

```sql
PRAGMA page_size;
```

**Output:**

```sql
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

```sql
sqlite> PRAGMA page_count;
3
sqlite>
```

Observation:
The database file size is 12KB because it contains 3 pages with each page using 4KB.

---

## mmap Experiment (SQLite)

### Check mmap

```sql
PRAGMA mmap_size;
```

**Output:**

```sql
0
```

### Enable mmap

```sql
PRAGMA mmap_size = 268435456;
```

### Query Time Comparison

```bash
time sqlite3 library-db "SELECT * FROM books;"
```

* mmap OFF:

```bash
1|Database Systems|Narendra|Education
2|Operating Systems|Eam|Education
3|Computer Networks|Eam1|Technology
4|Machine Learning|Eam3|AI
5|Python Basics|Eam3|Programming
6|Linux Guide|Eam3|Technology
7|Data Structures|Eam3|Education
8|Cyber Security|Eam3|Security
9|Cloud Computing|Eam3|Technology
10|Web Development|Eam3|Programming
11|Artificial Intelligence|Eam3|AI
12|Software Engineering|Eam3|Education
13|Big Data Analytics|Eam3|Data Science

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

* mmap ON:

```bash
1|Database Systems|Narendra|Education
2|Operating Systems|Eam|Education
3|Computer Networks|Eam1|Technology
4|Machine Learning|Eam3|AI
5|Python Basics|Eam3|Programming
6|Linux Guide|Eam3|Technology
7|Data Structures|Eam3|Education
8|Cyber Security|Eam3|Security
9|Cloud Computing|Eam3|Technology
10|Web Development|Eam3|Programming
11|Artificial Intelligence|Eam3|AI
12|Software Engineering|Eam3|Education
13|Big Data Analytics|Eam3|Data Science

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

Observation:
After enabling mmap with 256MB, the query execution became slightly faster (0.006s compared to 0.007s). This happens because memory mapping allows SQLite to access the database file more efficiently.

---

# PostgreSQL Experiment

## Create Database

```sql
CREATE DATABASE library-db;
\c library-db
```

---

## Create Table

```sql
CREATE TABLE books (
    book_id INT PRIMARY KEY,
    title TEXT NOT NULL,
    author TEXT NOT NULL,
    category TEXT NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO books VALUES (1, 'Database Systems', 'Narendra Sirvi', 'Education');
```

---

## Page Size

```sql
SELECT current_setting('block_size');
```

**Output:**

```sql
 current_setting
-----------------
 8192
(1 row)
```

PostgreSQL uses pages of size 8KB.

---

## Query Timing

```sql
\timing
SELECT * FROM books;
```

**Output:**

```sql
Timing is on.
 book_id |       title       |     author      |  category
---------+-------------------+------------------+------------
       1 | Database Systems  | Narendra Sirvi  | Education
(1 row)

Time: 0.185 ms
```

PostgreSQL executed the query in 0.185 ms, which is very efficient for a single record.

# System Architecture Check

## SQLite Process

```bash
ps aux | grep sqlite3
```

**Output:**

```bash
narendra   37129  0.0  0.0  18980  2344 pts/3    S+   18:42   0:00 grep --color=auto sqlite3
```

SQLite works as an embedded database library and does not require a separate server process.

---

## PostgreSQL Process

```bash
ps aux | grep postgres
```

**Output:**

```bash
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
narendra   37153  0.0  0.0  18980  2340 pts/3    S+   18:42   0:00 grep --color=auto postgres
```

PostgreSQL operates as a multi-process database server with several background services such as:

* checkpointer
* walwriter
* autovacuum
* background writer
* logical replication launcher

---

# Comparison Table

| Feature     | SQLite3           | PostgreSQL          |
| ----------- | ----------------- | ------------------- |
| Type        | Embedded Database | Client-Server DB    |
| Page Size   | 4KB               | 8KB                 |
| Storage     | Single File       | Managed Storage     |
| mmap        | Available         | Not Directly Used   |
| Performance | Lightweight       | Powerful & Scalable |
| Concurrency | Limited           | High                |

---

## Conclusion

SQLite3 is compact and easy to use because it stores the entire database in a single file and does not require a dedicated server. In this experiment, SQLite used 4KB pages, and enabling mmap slightly improved query speed.

PostgreSQL is a full-featured server-based database management system. It used 8KB pages and delivered very fast query execution. It is more suitable for enterprise applications and environments with many users.

In summary, SQLite3 is ideal for lightweight local applications, while PostgreSQL is more appropriate for large-scale and multi-user systems.
