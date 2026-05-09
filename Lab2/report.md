
# SQLite3 vs PostgreSQL Comparison Report

# SQLite3 Experiment

## Creating the Database

To start, create a new SQLite3 database:

```bash
sqlite3 lab-db
```

## Creating the Table

Define a `phonebook` table with columns for id, name, and phone number:

```sql
CREATE TABLE phonebook (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    phone_number TEXT NOT NULL
);
```

## Inserting Data

Insert a sample record into the phonebook:

```sql
INSERT INTO phonebook VALUES (1, "Alice", "123-456-7890");
```

---

## Checking File Size

To check the size of the database file:

```bash
ls -lh lab-db
```

**Sample Output:**

```
-rw-r--r-- 1 angel angel 12K May  9 18:46 lab-db
```

---

## Page Size and Page Count

To view the page size:

```sql
PRAGMA page_size;
```

**Sample Output:**

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

### Checking mmap

To see the current mmap setting:

```sql
PRAGMA mmap_size;
```

**Output:**

```
0
```

### Enabling mmap

Set mmap size to 256MB:

```sql
PRAGMA mmap_size = 268435456;
```

### Query Time Comparison

Run a query to compare performance with and without mmap:

```bash
time sqlite3 lab-db "SELECT * FROM phonebook;"
```

* mmap OFF:

```
1|Alice|123-456-7890
2|Bob|234-567-8901
3|Charlie|345-678-9012
4|David|456-789-0123
5|Eve|567-890-1234

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

* mmap ON:

```
1|Alice|123-456-7890
2|Bob|234-567-8901
3|Charlie|345-678-9012
4|David|456-789-0123
5|Eve|567-890-1234

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

Observation: Enabling mmap with 256MB slightly improves performance (0.006s vs 0.007s). mmap ON is a bit faster due to memory mapping.

---


# PostgreSQL Experiment

## Creating the Database

Create a new PostgreSQL database and connect to it:

```sql
CREATE DATABASE lab-db;
\c lab-db
```

---

## Creating the Table

Define the same `phonebook` table in PostgreSQL:

```sql
CREATE TABLE phonebook (
    id INT PRIMARY KEY,
    name TEXT NOT NULL,
    phone_number TEXT NOT NULL
);
```

---

## Inserting Data

Insert a sample record:

```sql
INSERT INTO phonebook VALUES (1, 'Alice', '123-456-7890');
```

---

## Page Size

Check the default page size:

```sql
SELECT current_setting('block_size');
```

**Sample Output:**

```
 current_setting 
-----------------
 8192
(1 row)
```

PostgreSQL uses 8KB pages.

---

## Query Timing

Enable timing and run a query:

```sql
	iming
SELECT * FROM phonebook;
```

**Sample Output:**

```
Timing is on.
 id |  name  |  phone_number  
----+--------+---------------
  1 | Alice  | 123-456-7890
(1 row)

Time: 0.185 ms
```

PostgreSQL query execution time: 0.185 ms (very fast for a single row).


# System Architecture Overview

## SQLite Process

To check the SQLite process:

```bash
ps aux | grep sqlite3
```

**Sample Output:**

```
angel   37129  0.0  0.0  18980  2344 pts/3    S+   18:42   0:00 grep --color=auto sqlite3
```

SQLite operates as an embedded library and does not require a separate server process.

---

## PostgreSQL Processes

To view PostgreSQL processes:

```bash
ps aux | grep postgres
```

**Sample Output:**

```
postgres   20324  0.0  0.1 235296 31808 ?        SNs  18:03   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
... (other processes) ...
angel  37153  0.0  0.0  18980  2340 pts/3    S+   18:42   0:00 grep --color=auto postgres
```

PostgreSQL runs as a multi-process server, including processes for:

* checkpointer
* walwriter
* autovacuum
* background writer
* logical replication launcher

---



# Feature Comparison Table

| Feature         | SQLite3         | PostgreSQL         |
| --------------- | --------------- | ------------------ |
| Type            | Embedded DB     | Client-Server DB   |
| Page Size       | 4KB             | 8KB                |
| Storage         | Single file     | Managed system     |
| mmap            | Supported       | Not exposed        |

## Conclusion

SQLite3 is a simple, lightweight database that stores all data in a single file and does not require a server. In this experiment, it used 4KB pages, and enabling mmap provided a slight performance boost.

PostgreSQL, on the other hand, is a server-based database system. It uses 8KB pages and delivered very fast query performance. PostgreSQL is more suitable for large-scale applications and environments with multiple users.



