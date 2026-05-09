# Lab 2 - SQLite3 and PostgreSQL Comparison

Name: Harshita Hirawat  
Roll Number: 10044

## 1 - SQLite3 Exploration

- Create or use a SQLite3 database with at least 2 tables.
- Insert sample data. Sakila DB can be used for this because it already contains multiple related tables and enough rows for meaningful observations.
- Check database file size:

```bash
ls -lh sakila.db
```

- Confirm that the database file size is a multiple of 4 KB.
- Check SQLite page size:

```sql
PRAGMA page_size;
```

- Expected default value: `4096` bytes.
- Check SQLite page count:

```sql
PRAGMA page_count;
```

- Observe page count growth after adding more data.
- Check default memory map size:

```sql
PRAGMA mmap_size;
```

- Expected default value: `0`.
- Enable timer and time a query:

```sql
.timer ON
SELECT * FROM users;
```

## 2 - Play With mmap_size

- Run a SELECT query with default mmap disabled:

```sql
PRAGMA mmap_size=0;
SELECT * FROM users;
```

- Enable mmap:

```sql
PRAGMA mmap_size=268435456;
SELECT * FROM users;
```

- Compare query time with and without mmap.
- Record whether there is a visible timing difference.

## 3 - TCP Loopback Research

- Research what TCP loopback means.
- Explain what happens when a Java application connects to:

```text
localhost:5432
```

- Mention whether the data touches the physical network card.

Expected conclusion: No, localhost traffic does not touch the physical NIC. It is handled inside the operating system kernel through the loopback network interface.

## 4 - PostgreSQL Setup and Comparison

- Install PostgreSQL.
- Load similar sample data into PostgreSQL.
- Check PostgreSQL page size:

```sql
SHOW block_size;
```

- Expected default value: `8192` bytes.
- Check PostgreSQL data directory:

```sql
SHOW data_directory;
```

- Observe PostgreSQL table storage as separate files inside the data directory.
- Observe PostgreSQL processes:

```bash
ps aux | grep postgres
```

- Time the same or similar query:

```sql
\timing on
SELECT * FROM users;
```
