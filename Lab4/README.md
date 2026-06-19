# SQLite Database Internal Structure Analysis

## Aim

The purpose of this experiment was to study how SQLite stores information internally by examining the raw hexadecimal dump of a database file.

The focus was on understanding:

- SQLite database header
- page organization
- B-tree node layout
- cell pointers
- storage addresses
- how record lookup works internally

---

## Tools Used

The following tools were used:

- SQLite3
- xxd
- strings
- grep
- macOS terminal

---

## Creating the Database

A custom SQLite database named `library.db` was created for this analysis.

### Tables Created

```sql
CREATE TABLE books (
    id INTEGER PRIMARY KEY,
    title TEXT,
    genre TEXT,
    pages INTEGER
);

CREATE TABLE authors (
    id INTEGER PRIMARY KEY,
    name TEXT,
    country TEXT
);
```

---

## Data Inserted

### Books

```text
1 | Clean Code | Programming | 464
2 | Dune | SciFi | 412
3 | 1984 | Dystopian | 328
4 | Atomic Habits | SelfHelp | 320
5 | Sapiens | History | 498
```

### Authors

```text
1 | Robert Martin | USA
2 | Frank Herbert | USA
3 | George Orwell | UK
```

Verification query:

```sql
SELECT * FROM books;
SELECT * FROM authors;
```

Output:

```text
1|Clean Code|Programming|464
2|Dune|SciFi|412
3|1984|Dystopian|328
4|Atomic Habits|SelfHelp|320
5|Sapiens|History|498

1|Robert Martin|USA
2|Frank Herbert|USA
3|George Orwell|UK
```

---

## Database Information

Commands executed:

```sql
PRAGMA page_size;
PRAGMA page_count;
.tables
.schema
```

Output:

```text
Page Size: 4096 bytes
Page Count: 3
Tables: authors, books
```

Observation:

SQLite divided the database into 3 pages of 4096 bytes each.

Address mapping:

| Page | Hex Address Range |
|------|------------------|
| Page 1 | 0x0000 – 0x0FFF |
| Page 2 | 0x1000 – 0x1FFF |
| Page 3 | 0x2000 – 0x2FFF |

---

## Hex Dump Generation

Command used:

```bash
xxd library.db > dump.txt
```

Preview:

```bash
head -30 dump.txt
```

Beginning of dump:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
00000010: 1000 0101 0c40 2020 0000 0004 0000 0003
00000020: 0000 0000 0000 0000 0000 0002 0000 0004
```

---

## SQLite Header Analysis

The first section of the file contains the SQLite metadata.

### Interpretation

| Offset | Value | Description |
|--------|------|-------------|
| 0x00 | SQLite format 3 | File signature |
| 0x10 | 1000 | Page size (4096 bytes) |
| 0x12 | 01 | Write version |
| 0x13 | 01 | Read version |
| 0x1C | 00000003 | Total number of pages |

This matches the PRAGMA output.

---

## Schema Storage

Using:

```bash
strings library.db
```

Important output:

```text
CREATE TABLE authors
CREATE TABLE books
```

This confirms that SQLite stores schema definitions inside the database file itself.

---

## Table Location Identification

Search commands:

```bash
grep -i "books" dump.txt
grep -i "authors" dump.txt
```

Output:

```text
00000f80: tablebooksbooks
00000f10: tableauthorsauthors
```

This indicates schema metadata is stored in page 1.

The root pages are:

- books → Page 2
- authors → Page 3

---

## B-Tree Structure

SQLite stores table records in B-tree pages.

Common page types:

| Hex Code | Meaning |
|---------|---------|
| 0D | Leaf Table Page |
| 05 | Interior Table Page |

In this database, both data pages are leaf table pages.

---

## Analysis of Books Page

Books table starts at Page 2.

Command:

```bash
xxd -s 0x1000 library.db | head
```

Output:

```text
00001000: 0d00 0000 050f 7900 0fd6 0fc4 0fae 0f90
```

Interpretation:

- `0D` → leaf table B-tree page
- `0005` → 5 rows
- `0F79` → beginning of content storage

Cell pointers:

```text
0FD6
0FC4
0FAE
0F90
0F79
```

These pointers represent offsets to individual book records.

---

## Example Record Location

Searching for:

```bash
grep -i "Dune" dump.txt
```

Output:

```text
00001fc0: ... Dune ...
```

This shows that actual row data is physically stored inside the database file.

---

## Analysis of Authors Page

Authors table starts at Page 3.

Command:

```bash
xxd -s 0x2000 library.db | head
```

Output:

```text
00002000: 0d00 0000 030f b300 0fde 0fc8 0fb3
```

Interpretation:

- `0D` → leaf table page
- `0003` → 3 records
- `0FB3` → content area start

Cell pointers:

```text
0FDE
0FC8
0FB3
```

These point to author records.

---

## Example Author Record

Search:

```bash
grep -i "Orwell" dump.txt
```

Output:

```text
00002fc0: OrwellUK
```

This confirms author data is also stored directly inside the page payload.

---

## Record Navigation

SQLite records typically contain:

- payload size
- row id
- header metadata
- type information
- actual field values

For example:

```text
2 | Dune | SciFi | 412
```

SQLite stores:
- integer values
- text strings
- internal metadata describing field layout

---

## Lookup Process

For a query such as:

```sql
SELECT * FROM books WHERE id = 2;
```

SQLite internally:

1. Opens the root page of the books table
2. Reads the B-tree page header
3. Checks the cell pointer array
4. Locates the correct row ID
5. Reads the corresponding payload
6. Reconstructs the original row

---

## Internal Layout Summary

```text
library.db
│
├── Page 1
│   ├── SQLite file header
│   ├── schema metadata
│   └── table definitions
│
├── Page 2
│   ├── books B-tree page
│   ├── row pointers
│   └── book records
│
└── Page 3
    ├── authors B-tree page
    ├── row pointers
    └── author records
```

---

## Conclusion

This experiment helped in understanding SQLite storage internals beyond normal SQL usage.

Important observations:

- SQLite stores everything in fixed pages
- schema definitions are part of the database file
- tables are maintained using B-tree structures
- pointers are used to navigate records
- actual text data can be located in the hex dump
- SQL queries are resolved by traversing these internal structures