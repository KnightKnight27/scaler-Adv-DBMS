# Lab 2 - SQLite3 and PostgreSQL Comparison

Name: Abhijit P
Roll No: 24bcs10175

## Objective

The objective of this assignment was to explore SQLite3 and PostgreSQL by performing basic database operations and comparing their behavior.

The assignment includes:

* database creation
* table creation
* inserting sample data
* running queries
* checking page size and page count
* checking mmap_size
* measuring query execution time
* comparing SQLite3 and PostgreSQL

---

## SQLite3 Work

SQLite3 was used to create a local file-based database named `football_clubs`.

### Tables Created

### football_clubs

| id | name                 | location | ucl |
| -- | -------------------- | -------- | --- |
| 1  | Barcelona FC         | Spain    | 5   |
| 2  | Real Madrid FC       | Spain    | 15  |
| 3  | FC Bayern Munich     | Germany  | 6   |
| 4  | Liverpool FC         | England  | 6   |
| 5  | Manchester United FC | England  | 3   |
| 6  | Manchester City FC   | England  | 1   |
| 7  | Arsenal FC           | England  | 0   |

### movies

| id | title                               | imdb_rating |
| -- | ----------------------------------- | ----------- |
| 1  | Oppenheimer                         | 8.3         |
| 2  | Dune: Part Two                      | 8.5         |
| 3  | Spider-Man: Across the Spider-Verse | 8.6         |
| 4  | Interstellar                        | 8.7         |

---

## SQLite3 Storage Analysis

### Database File Size

```bash id="6bhm6o"
-rwxrwxrwx 1 adx adx 12K May 9 05:05 football_clubs
```

### Page Size

Observed page size:

```text id="c67k7v"
4096 bytes
```

### Page Count

Observed page count:

```text id="j1a5y3"
3
```

Page count increased after inserting more data into the database.

### mmap_size

Default mmap_size:

```text id="76z1xw"
0
```

Updated mmap_size:

```text id="3z8h8d"
268435456
```

---

## SQLite3 Query Timing

### Query Used

```sql id="fjlwm6"
SELECT * FROM football_clubs;
```

### Without mmap

```text id="jlwm7"
real    0m0.009s
user    0m0.003s
sys     0m0.000s
```

### With mmap

```text id="jlwm8"
real    0m0.009s
user    0m0.003s
sys     0m0.000s
```

For this small dataset, enabling mmap did not produce a noticeable performance improvement.

---

## PostgreSQL Work

PostgreSQL was installed and tested using the same football_clubs dataset.

### PostgreSQL Query Timing

```text id="jlwm9"
real    0m0.039s
user    0m0.006s
sys     0m0.004s
```

---

## SQLite3 vs PostgreSQL Comparison

| Feature       | SQLite3                           | PostgreSQL                  |
| ------------- | --------------------------------- | --------------------------- |
| Database Type | File-based database               | Server-based database       |
| Setup         | Lightweight and simple            | Requires PostgreSQL server  |
| Performance   | Fast for small/local applications | Better for scalable systems |
| Concurrency   | Limited                           | Better concurrency support  |
| Usage         | Embedded/local applications       | Enterprise applications     |

---

## Environment Used

The assignment was tested in Linux/WSL environment using bash.

Note:
PostgreSQL setup used Linux-specific authentication with:

```bash id="jlwm10"
sudo -u postgres
```

---

## Conclusion

SQLite3 is lightweight, simple, and useful for local applications and testing.

PostgreSQL is more suitable for scalable applications and systems requiring better concurrency and server-based architecture.

The assignment helped in understanding:

* database storage concepts
* page size and page count
* mmap usage
* query timing
* SQLite3 internals
* comparison between SQLite3 and PostgreSQL
