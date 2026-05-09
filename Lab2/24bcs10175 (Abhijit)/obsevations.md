# Observations

## SQLite3 Observations

### Page Size

Observed page size:

4096 bytes

### Page Count

Observed page count:

3

Page count increased after inserting more data into the database.

### mmap_size

Default mmap_size:

0

Updated mmap_size:

268435456

---

## SQLite Query Timing

### Without mmap

real    0m0.009s  
user    0m0.003s  
sys     0m0.000s

### With mmap

real    0m0.009s  
user    0m0.003s  
sys     0m0.000s

For this small dataset, enabling mmap did not produce a noticeable performance improvement.

---

## PostgreSQL Query Timing

real    0m0.039s  
user    0m0.006s  
sys     0m0.004s

---

## SQLite3 vs PostgreSQL Comparison

| Feature | SQLite3 | PostgreSQL |
|---|---|---|
| Type | File-based database | Server-based database |
| Setup | Lightweight and simple | Requires PostgreSQL server |
| Performance | Fast for small/local applications | Better for larger scalable systems |
| Concurrency | Limited | Better concurrency support |
| Usage | Embedded/local applications | Enterprise applications |

---

## Conclusion

SQLite3 is simple and lightweight for local applications and quick testing.

PostgreSQL is more suitable for scalable applications and systems requiring better concurrency and server-based architecture.
