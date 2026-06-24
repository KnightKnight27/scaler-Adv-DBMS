# Comparative Study of SQLite3 and PostgreSQL

# SQLite3 Practical Experiment

## Database Creation

```bash
sqlite3 lab-db
```

## Creating the Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

## Adding Records

```sql
INSERT INTO students VALUES (1, "Prisha", 19, "C-");
```

---

## Checking Database File Size

```bash
ls -lh lab-db
```

### Result

```bash
-rw-r--r-- 1 prisha prisha 12K May  9 19:46 lab-db
```

---

## Determining Page Size and Total Pages

### Page Size

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

### Page Count

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

The database occupies around **12 KB**, which corresponds to **3 pages of 4 KB each**.

---

# Memory Mapping (mmap) Experiment in SQLite

## Viewing Current mmap Size

```sql
PRAGMA mmap_size;
```

### Output

```sql
0
```

## Activating mmap

```sql
PRAGMA mmap_size = 268435456;
```

---

## Comparing Query Execution Time

```bash
time sqlite3 lab-db "SELECT * FROM students;"
```

### With mmap Disabled

```bash
1|Prisha|19|C-
2|Eam|19|C-
3|Eam1|19|C-
4|Eam3|19|C-

real    0m0.007s
user    0m0.005s
sys     0m0.003s
```

### With mmap Enabled

```bash
1|Prisha|19|C-
2|Eam|19|C-
3|Eam1|19|C-
4|Eam3|19|C-

real    0m0.006s
user    0m0.003s
sys     0m0.003s
```

### Observation

After enabling **memory mapping (256 MB)**, query execution became marginally faster. This happens because mmap allows SQLite to access the database file directly through memory instead of traditional file I/O operations.

---

# PostgreSQL Practical Experiment

## Creating the Database

```sql
CREATE DATABASE lab-db;
\c lab-db
```

## Creating the Students Table

```sql
CREATE TABLE students (
    roll_no INT PRIMARY KEY,
    fullname TEXT NOT NULL,
    age INT NOT NULL,
    grade TEXT NOT NULL
);
```

## Inserting Sample Data

```sql
INSERT INTO students VALUES (1, 'Prisha Gupta', 19, 'A+');
```

## Checking PostgreSQL Page Size

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

PostgreSQL internally uses **8 KB blocks/pages** for data storage.

---

# Final Conclusion

SQLite3 is lightweight, easy to set up, and suitable for small applications. PostgreSQL, however, is more scalable and reliable for enterprise-level systems with multiple users and higher concurrency requirements.
