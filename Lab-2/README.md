# SQLite3 vs PostgreSQL Comparison Report 



## Create Database

```powershell
sqlite3 ojas-db.db
```

---

## Create Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO students VALUES (1, "Ojas", 19, "A");
```

---

# File Size Check

```powershell
dir ojas-db.db
```

### Output

```powershell
 Directory: C:\Users\Ojas\sqlite

Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
-a----         09-05-2026  08:46 PM         12288 ojas-db.db
```

---

# Page Size & Page Count

```sql
PRAGMA page_size;
```

### Output

```sql
sqlite> PRAGMA page_size;
4096
```

---

```sql
PRAGMA page_count;
```

### Output

```sql
sqlite> PRAGMA page_count;
3
```

### Observation

12KB file = 3 pages × 4KB



# mmap 

## Check mmap

```sql
PRAGMA mmap_size;
```

### Output

```sql
0
```

---

## Enable mmap

```sql
PRAGMA mmap_size = 268435456;
```

---

# Query Time 

## mmap OFF

```powershell
Measure-Command { .\sqlite3.exe ojas-db.db "SELECT * FROM students;" }
```

### Output

```powershell
Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 0
Milliseconds      : 22
Ticks             : 221069
TotalMilliseconds : 22.1069
```

---

## mmap ON

```powershell
Measure-Command { .\sqlite3.exe ojas-db.db "SELECT * FROM students;" }
```

### Output

```powershell
Days              : 0
Hours             : 0
Minutes           : 0
Seconds           : 0
Milliseconds      : 18
Ticks             : 180540
TotalMilliseconds : 18.054
```

---

### Observation

mmap enabled with 256MB improves performance slightly (18ms vs 22ms). mmap ON is slightly faster because the database file is mapped directly into memory.

---

# PostgreSQL Experiment

## Create Database

```sql
CREATE DATABASE ojas_db;
```

Connect to database:

```sql
\c ojas_db
```

---

## Create Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

---

## Insert Data

```sql
INSERT INTO students VALUES (1, 'Ojas', 19, 'A+');
```

---

# Page Size

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

PostgreSQL uses 8KB pages.

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
       1 | Ojas     |  19 | A+
(1 row)

Time: 0.185 ms
```

---

# System Architecture Check

## SQLite Process

```powershell
Get-Process sqlite3
```

### Output

```powershell
Handles  NPM(K)    PM(K)      WS(K)     CPU(s)     Id ProcessName
-------  ------    -----      -----     ------     -- -----------
     50       5     2400       5000       0.01  14320 sqlite3
```

### Observation

SQLite runs as an embedded database and does not require a separate server.

---

## PostgreSQL Process

```powershell
Get-Process postgres
```

### Output

```powershell
Handles  NPM(K)    PM(K)      WS(K)     CPU(s)     Id ProcessName
-------  ------    -----      -----     ------     -- -----------
    210      15    35000      42000       0.15   8024 postgres
    180      12    28000      31000       0.09   8040 postgres
    170      11    26000      29500       0.07   8052 postgres
```

### Observation

PostgreSQL runs as a multi-process server with background services.

---

# Comparison Table

| Feature     | SQLite3     | PostgreSQL       |
| ----------- | ----------- | ---------------- |
| Type        | Embedded DB | Client-Server DB |
| Page Size   | 4KB         | 8KB              |
| Storage     | Single File | Managed System   |
| mmap        | Supported   | Not Exposed      |
| Performance | Lightweight | High Performance |
| Concurrency | Low         | High             |

---

# Conclusion

SQLite3 is simple and lightweight because it stores data in a single file and does not require a server. In this experiment, it used 4KB pages, and enabling mmap slightly improved query performance.

PostgreSQL is a server-based database system. It used 8KB pages and executed queries very quickly. It is more suitable for large applications and multiple users.

Overall:

* SQLite3 is better for small local applications.
* PostgreSQL is better for large-scale and multi-user systems.
