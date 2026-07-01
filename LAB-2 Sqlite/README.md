# Playing with SQLite

- Uses SQL to query.
- OS eviction policy is not optimal as it does not know about the next in line queries or expected pages.
  - nmap helps to take that control back.
  - The Page Cache is copied into the memory of the sqlite process, giving authority of optimisation to SQLite.
  - We can bypass the overhead of the standard read and write syscalls.
  - Can be changed via the `PRAGMA nmap_size`

- Page size is smalledst unit of disk IO.
- 4KB by default inside SQLite
- Size of the database = `#pages * page_size`
- And BTW everything is stored inside a single file, even if there are multiple tables.
- We can see query times with `.timer on`

```
CREATE TABLE test(id INTEGER, val TEXT);
    INSERT INTO test SELECT value, 'data' FROM generate_series(1, 100000);
Run Time: real 0.052 user 0.017178 sys 0.000000
```

| Run Time: real 0.049 user 0.044108 sys 0.003138 | Run Time: real 0.042 user 0.040842 sys 0.000000 |

| mmap size | run time (real, s) |
| --------- | ------------------ |
| 0         | 0.064              |
| 512MB     | 0.042              |