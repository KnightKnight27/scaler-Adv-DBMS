# SQLite3 vs PostgreSQL Comparative Study

# SQLite3 Experiment

## Database Creation

```bash
sqlite3 lab-db
```

---

## Creating the Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

---

## Inserting Data

```sql
INSERT INTO students VALUES (1, "Pranay", 19, "C-");
```

---

# Checking Database File Size

```bash
ls -lh lab-db
```

### Output

```bash
-rw-r--r-- 1 pranay pranay 12K May 9 18:46 lab-db
```

---

# Page Size and Page Count

## Page Size

```sql
PRAGMA page_size;
```

### Output

```sql
SQLite version 3.45.1 2024-01-30 16:01:20
Enter ".help" for usage hints.
sqlite> PRAGMA page_size;
4096
sqlite>
```

---

## Page Count

```sql
PRAGMA page_count;
```

### Output

```sql
sqlite> PRAGMA page_count;
3
sqlite>
```

### Observation

The database occupies 12KB of storage:

```text
3 pages × 4KB = 12KB
```

---

# SQLite mmap Experiment

## Checking mmap Configuration

```sql
PRAGMA mmap_size;
```

### Output

```sql
0
```

---

## Enabling mmap

```sql
PRAGMA mmap_size = 268435456;
```

---

# Query Execution Time Comparison

```bash
time sqlite3 lab-db "SELECT * FROM students;"
```

## mmap Disabled

```bash
1|Pranay|19|C-
2|Eam|19|C-
3|Eam1|19|C-
4|Eam3|19|C-
5|Eam3|19|C-
6|Eam3|19|C-
7|Eam3|19|C-
8|Eam3|19|C-
9|Eam3|19|C-
10|Eam3|19|C-
11|Eam3|19|C-
12|Eam3|19|C-
13|Eam3|19|C-

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

---

## mmap Enabled

```bash
1|Pranay|19|C-
2|Eam|19|C-
3|Eam1|19|C-
4|Eam3|19|C-
5|Eam3|19|C-
6|Eam3|19|C-
7|Eam3|19|C-
8|Eam3|19|C-
9|Eam3|19|C-
10|Eam3|19|C-
11|Eam3|19|C-
12|Eam3|19|C-
13|Eam3|19|C-

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

### Observation

After enabling memory mapping with a limit of 256MB, query execution became slightly faster (0.006s compared to 0.007s). mmap improves performance by mapping the database file directly into memory.

---

# PostgreSQL Experiment

## Creating the Database

```sql
CREATE DATABASE lab-db;
\c lab-db
```

---

## Creating the Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

---

## Inserting Data

```sql
INSERT INTO students VALUES (1, 'Pranay', 19, 'A+');
```

---

# Checking PostgreSQL Page Size

```sql
SELECT current_setting('block_size');
```

### Output

```sql
 current_setting
-----------------
 8192
(1 row)
```

### Observation

PostgreSQL uses an 8KB block size by default.

---

# Query Timing

```sql
\timing
SELECT * FROM students;
```

### Output

```sql
Timing is on.
 roll_no | fullname | age | grade
---------+----------+-----+-------
       1 | Pranay   |  19 | A+
(1 row)

Time: 0.185 ms
```

### Observation

The query executed in 0.185 ms, showing PostgreSQL’s efficiency even for small datasets.

---

# System Architecture Analysis

## SQLite Process Check

```bash
ps aux | grep sqlite3
```

### Output

```bash
pranay   37129  0.0  0.0  18980  2344 pts/3    S+   18:42   0:00 grep --color=auto sqlite3
```

### Observation

SQLite functions as an embedded database library and does not require a dedicated server process.

---

## PostgreSQL Process Check

```bash
ps aux | grep postgres
```

### Output

```bash
postgres   20324  0.0  0.1 235296 31808 ?        SNs  18:03   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres   20325  0.0  0.2 235588 32936 ?        SNs  18:03   0:00 postgres: 16/main: checkpointer
postgres   20326  0.0  0.0 235452  8180 ?        SNs  18:03   0:00 postgres: 16/main: background writer
postgres   20328  0.0  0.0 235296 10792 ?        SNs  18:03   0:00 postgres: 16/main: walwriter
postgres   20329  0.0  0.0 236908  9420 ?        SNs  18:03   0:00 postgres: 16/main: autovacuum launcher
postgres   20330  0.0  0.0 236880  8588 ?        SNs  18:03   0:00 postgres: 16/main: logical replication launcher
```

### Observation

PostgreSQL follows a multi-process server architecture and runs multiple background services such as:

- Checkpointer
- WAL Writer
- Background Writer
- Autovacuum Launcher
- Logical Replication Launcher

These processes improve reliability, concurrency handling, and crash recovery.

---

# Comparison Table

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Database Type | Embedded Database | Client-Server Database |
| Page Size | 4KB | 8KB |
| Storage | Single File | Managed Storage System |
| mmap Support | Supported | Not Directly Exposed |
| Performance | Lightweight and Fast | Highly Scalable |
| Concurrency | Limited | High |
| Server Requirement | No | Yes |

---

# Conclusion

SQLite3 is a lightweight embedded database system that stores data in a single file and does not require a separate server process. It is simple to configure, resource-efficient, and suitable for local or small-scale applications. The experiment also demonstrated a minor performance improvement after enabling mmap.

PostgreSQL is a robust client-server relational database designed for scalability, reliability, and concurrent access. Its architecture includes several background processes that enhance performance and database management capabilities.

Overall:

- SQLite3 is ideal for lightweight applications, embedded systems, and local storage.
- PostgreSQL is better suited for enterprise applications, large datasets, and multi-user environments.