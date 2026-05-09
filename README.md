# scaler-Adv-DBMS

# DBMS Lab Exploration

## SQLite3 Exploration

### SQLite Version

```bash
sqlite3 --version
```

Output:

```bash
3.51.0
```

---

## Database Creation

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users (name, email)
VALUES
('Tanmay', 'a@test.com'),
('John', 'b@test.com'),
('Alice', 'c@test.com');
```

---

## File Size Observation

```bash
ls -lh
```

Observation:

- SQLite stores the database in a single file (`test.db`)
- File size was very small because dataset was minimal

---

## PRAGMA Commands

### Page Size

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

### Page Count

```sql
PRAGMA page_count;
```

Output:

```text
2
```

### mmap_size

Initial value:

```sql
PRAGMA mmap_size;
```

Output:

```text
0
```

After modification:

```sql
PRAGMA mmap_size = 268435456;
```

Output:

```text
268435456
```

Observation:

- mmap_size can be changed dynamically
- For this small database, no major performance difference was observed

---

## Query Execution Time

```bash
time sqlite3 test.db "SELECT * FROM users;"
```

Output:

```text
0.008 total
```

Observation:

- Query execution was extremely fast for small dataset

---

## Process Observation

```bash
ps aux | grep sqlite
```

Observation:

- SQLite process appears temporarily during execution

---

# PostgreSQL Exploration

## PostgreSQL Version

```bash
psql --version
```

Output:

```bash
14.19
```

---

## Database Creation

```sql
CREATE TABLE users (
    id SERIAL PRIMARY KEY,
    name TEXT,
    email TEXT
);

INSERT INTO users(name, email)
VALUES
('Tanmay', 'a@test.com'),
('John', 'b@test.com'),
('Alice', 'c@test.com');
```

---

## Block Size

```sql
SHOW block_size;
```

Output:

```text
8192
```

---

## Page Count

```sql
SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

Output:

```text
0
```

Observation:

- Very small tables may show 0 pages initially

---

## Query Execution Time

```sql
\timing
SELECT * FROM users;
```

Output:

```text
Time: 0.829 ms
```

Observation:

- PostgreSQL query execution was also very fast

---

# SQLite vs PostgreSQL Comparison

| Feature           | SQLite                         | PostgreSQL                 |
| ----------------- | ------------------------------ | -------------------------- |
| Type              | Embedded Database              | Client-Server Database     |
| Setup Complexity  | Very Easy                      | Moderate                   |
| Storage           | Single File                    | Server Managed             |
| Page Size         | 4096 bytes                     | 8192 bytes                 |
| Page Count        | 2                              | 0                          |
| mmap Support      | Direct PRAGMA support          | Internal memory management |
| Query Performance | Very fast for small local data | Very fast and scalable     |
| Best Use Case     | Lightweight applications       | Large scalable systems     |

---

# Final Conclusion

SQLite is lightweight, simple, and easy to use because it operates directly using a local database file.

PostgreSQL is more powerful and scalable, but requires a running database server and additional setup.

For small applications and local storage, SQLite is very convenient. For production systems and larger applications, PostgreSQL is generally the better choice.
