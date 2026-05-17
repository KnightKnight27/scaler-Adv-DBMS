# SQLite3 vs PostgreSQL Comparison Report

## Objective

The objective of this lab was to compare SQLite3 and PostgreSQL by observing storage behavior, page size, query performance, and mmap functionality.

---

# Part 1: SQLite3 Experiments

## Database Creation

A sample database named `sample.db` was created with a `users` table.

### Commands Used

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);

INSERT INTO users (name, age)
VALUES
('Archit', 20),
('Rahul', 21),
('Priya', 22);
```

---

## File Size Observation

### Command

```cmd
dir
```

### Observation

The database file size was:

```text
8192 bytes (8 KB)
```

---

## Page Size and Page Count

### Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
```

### Output

| Property   | Value      |
| ---------- | ---------- |
| Page Size  | 4096 bytes |
| Page Count | 2          |

### Analysis

```text
4096 × 2 = 8192 bytes
```

This confirmed that SQLite stores data in fixed-size pages.

---

## mmap Experiment

### Commands

```sql
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

Initial mmap size was `0`, meaning mmap was disabled by default.

---

## Query Timing

### Enable Timer

```sql
.timer on
```

### Query

```sql
SELECT * FROM users;
```

### Observed Timings

| Mode         | Query Time    |
| ------------ | ------------- |
| With mmap    | ~0.010884 sec |
| Without mmap | ~0.011229 sec |

### Observation

The timing difference was very small because the database size was extremely small.

---

## Process Observation

### Command

```cmd
tasklist | findstr sqlite
```

### Observation

SQLite used very low memory, showing its lightweight embedded architecture.

---

# Part 2: PostgreSQL Experiments

## Database Creation

A database named `labdb` was created.

### Commands Used

```sql
CREATE DATABASE labdb;

\c labdb

CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name VARCHAR(50),
    age INT
);

INSERT INTO users(name, age)
VALUES
('Archit',20),
('Rahul',21),
('Priya',22);
```

---

## Block Size Observation

### Command

```sql
SHOW block_size;
```

### Output

```text
8192 bytes
```

PostgreSQL uses 8 KB blocks/pages by default.

---

## Page Count Observation

### Commands

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

After running:

```sql
ANALYZE users;
```

The page count became:

```text
1
```

---

## Query Timing

### Enable Timing

```sql
\timing
```

### Query

```sql
SELECT * FROM users;
```

### Observed Timing

```text
0.892 ms
```

---

# SQLite3 vs PostgreSQL Comparison

| Feature           | SQLite3           | PostgreSQL              |
| ----------------- | ----------------- | ----------------------- |
| Architecture      | Embedded database | Client-server database  |
| Default Page Size | 4096 bytes        | 8192 bytes              |
| Storage           | Single `.db` file | Multiple internal files |
| mmap Support      | Configurable      | Internally managed      |
| Resource Usage    | Low               | Higher                  |
| Best Use Case     | Lightweight apps  | Large scalable systems  |

---

# Key Observations

1. SQLite is lightweight and easy to set up.
2. PostgreSQL provides a more scalable server-based architecture.
3. Both databases allocate storage using fixed-size pages.
4. mmap showed minimal performance difference because the dataset was very small.
5. PostgreSQL showed faster measured query timing in this experiment.

---

# Conclusion

This lab helped in understanding how SQLite3 and PostgreSQL manage storage and query execution internally.

SQLite is suitable for lightweight local applications, while PostgreSQL is better suited for larger and scalable systems. The experiments also provided practical understanding of page allocation, mmap behavior, and query performance.
