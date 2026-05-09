# scaler-Adv-DBMS

# DBMS Lab Exploration

## SQLite3 Exploration

### Commands Used

```bash
sqlite3 test.db
ls -lh
```

### PRAGMA Commands

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA mmap_size;
PRAGMA mmap_size = 268435456;
```

### Observations

- SQLite database file was created locally as `test.db`
- Page size observed: 4096 bytes
- Page count was very small because the database had limited data
- mmap_size could be modified successfully
- Query execution time difference was minimal due to small dataset

### Query Timing

```bash
time sqlite3 test.db "SELECT * FROM users;"
```

### Process Observation

```bash
ps aux | grep sqlite
```

- SQLite process appeared temporarily during query execution

---

# PostgreSQL Exploration

## Commands Used

```bash
createdb testdb
psql testdb
```

### PostgreSQL Queries

```sql
SHOW block_size;

SELECT relpages
FROM pg_class
WHERE relname = 'users';
```

### Observations

- PostgreSQL uses fixed-size blocks/pages
- Default page size observed: 8192 bytes
- Query execution was fast for small dataset
- PostgreSQL required server setup unlike SQLite

### Query Timing

```sql
\timing
SELECT * FROM users;
```

---

# SQLite vs PostgreSQL Comparison

| Feature           | SQLite                         | PostgreSQL                           |
| ----------------- | ------------------------------ | ------------------------------------ |
| Type              | Embedded DB                    | Client-Server DB                     |
| Setup             | Very simple                    | More complex                         |
| Page Size         | 4096 bytes                     | 8192 bytes                           |
| Storage           | Single file                    | Managed server storage               |
| mmap Support      | Yes                            | Different internal memory management |
| Query Performance | Fast for small local workloads | Better for scalable workloads        |
| Best Use Case     | Lightweight apps               | Large production systems             |

---

# Final Conclusion

SQLite is lightweight and easy to use because it stores everything in a single file and does not require a server.

PostgreSQL is more powerful and scalable but requires proper installation and server management.

For small applications SQLite is convenient, while PostgreSQL is better suited for enterprise-level systems.
