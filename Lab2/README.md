# SQLite3 vs PostgreSQL Comparison Report

## SQLite3 Experiments

### Database File Size

Command Used:

```bash
ls -lh
```

Observed File Size:

- sample.db → 8.0K

---

### Page Size

Command Used:

```sql
PRAGMA page_size;
```

Result:

- 4096 bytes

---

### Page Count

Command Used:

```sql
PRAGMA page_count;
```

Result:

- 2 pages

---

### mmap_size

Default mmap_size:

```sql
PRAGMA mmap_size;
```

Result:

- 0

Changed mmap_size:

```sql
PRAGMA mmap_size = 268435456;
```

New Result:

- 268435456 bytes (256 MB)

---

### Query Execution Time WITHOUT mmap

Command Used:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Result:

- real → 0m0.007s
- user → 0m0.003s
- sys → 0m0.004s

---

### Query Execution Time WITH mmap

Command Used:

```bash
time sqlite3 sample.db "SELECT * FROM users;"
```

Result:

- real → 0m0.007s
- user → 0m0.004s
- sys → 0m0.004s

---
### Process Monitoring
```bash
ps aux | grep sqlite
```
Result:
```
sarthak+   12510  0.0  0.0   9144  2132 pts/1    S+   19:38   0:00 grep --color=auto sqlite
```
### Observation on mmap

For this small database, enabling mmap did not significantly improve performance because the dataset is extremely small.

---

## PostgreSQL Experiments

### Page Size

Command Used:

```sql
SHOW block_size;
```

Result:

- 8192 bytes

---

### Relation Size

Command Used:

```sql
SELECT pg_relation_size('users');
```

Result:

- 8192 bytes

---

### Page Count

Command Used:

```sql
SELECT pg_relation_size('users') / current_setting('block_size')::int;
```

Result:

- 1 page

---

### Query Execution Time

Command Used:

```sql
\timing
SELECT * FROM users;
```

Result:

- 0.732 ms

---

## Process Observation


### Process Monitoring 

Command Used:

```bash
ps aux | grep postgres
```
Result:
```
postgres   15551  0.0  0.2 225456 30980 ?        Ss   19:41   0:00 /usr/lib/postgresql/16/bin/postgres -D /var/lib/postgresql/16/main -c config_file=/etc/postgresql/16/main/postgresql.conf
postgres   15552  0.0  0.0 225588  5944 ?        Ss   19:41   0:00 postgres: 16/main: checkpointer 
postgres   15553  0.0  0.0 225612  7668 ?        Ss   19:41   0:00 postgres: 16/main: background writer 
postgres   15555  0.0  0.0 225456  9920 ?        Ss   19:41   0:00 postgres: 16/main: walwriter 
postgres   15556  0.0  0.0 227064  8924 ?        Ss   19:41   0:00 postgres: 16/main: autovacuum launcher 
postgres   15557  0.0  0.0 227040  8156 ?        Ss   19:41   0:00 postgres: 16/main: logical replication launcher 
root       16034  0.0  0.0  19808  7776 pts/2    S+   19:43   0:00 sudo -i -u postgres
root       16038  0.0  0.0  19808  2744 pts/3    Ss   19:43   0:00 sudo -i -u postgres
postgres   16039  0.0  0.0  11256  5172 pts/3    S    19:43   0:00 -bash
postgres   16090  0.0  0.0  26080  8924 pts/3    S+   19:44   0:00 /usr/lib/postgresql/16/bin/psql
postgres   16182  0.0  0.1 228200 20200 ?        Ss   19:44   0:00 postgres: 16/main: postgres labdb [local] idle
sarthak+   16732  0.0  0.0   9144  2132 pts/5    S+   19:45   0:00 grep --color=auto postgres
```

Observation:

- PostgreSQL runs multiple background services continuously:
  - checkpointer
  - background writer
  - walwriter
  - autovacuum launcher
  - logical replication launcher

---

# Comparison Table

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Architecture | File-based | Client-Server |
| Page Size | 4096 bytes | 8192 bytes |
| Page Count | 2 | 1 |
| Query Time | 0.007s | 0.732 ms |
| mmap Support | Yes | Internal memory management |
| Background Processes | No | Yes |
| Setup Complexity | Simple | More complex |

---

# Conclusion

SQLite3 is lightweight, simple, and suitable for small applications or embedded systems. It stores the database directly as a file and has minimal resource usage.

PostgreSQL is a full-featured relational database system with background services, better scalability, and advanced features suitable for enterprise applications.

For small datasets, mmap did not noticeably improve SQLite performance. PostgreSQL showed very fast query execution and provides more advanced database management capabilities.