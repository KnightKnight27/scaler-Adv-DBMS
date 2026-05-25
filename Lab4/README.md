# SQLite3 Internal Storage Analysis using xxd

## Objective

The goal of this lab was to examine how SQLite stores data internally at the binary level using hexadecimal inspection.

The tasks included:

- creating a custom SQLite database
- generating a hexadecimal dump using `xxd`
- identifying SQLite file header fields
- understanding page structure
- locating B-tree nodes and cell pointers
- tracing actual stored records
- understanding how SQLite performs record lookup

---

## Tools Used

- SQLite3
- xxd
- strings
- grep
- macOS Terminal

---

## Database Creation

For this experiment, I created a custom database called `library.db`.

### Schema

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

## Sample Data Inserted

### Books Table

```text
1 | Clean Code    | Programming | 464
2 | Dune          | SciFi       | 412
3 | 1984          | Dystopian   | 328
4 | Atomic Habits | SelfHelp    | 320
5 | Sapiens       | History     | 498
```

### Authors Table

```text
1 | Robert Martin | USA
2 | Frank Herbert | USA
3 | George Orwell | UK
```

Verification:

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

## Database Properties

Commands used:

```sql
PRAGMA page_size;
PRAGMA page_count;
.tables
.schema
```

Output:

```text
page_size = 4096
page_count = 3
tables = authors, books
```

Observation:
- SQLite divided the database into 3 pages.
- Each page is 4096 bytes.

So memory layout becomes:

| Page Number | Address Range |
|-----------|--------------|
| Page 1 | 0x0000 – 0x0FFF |
| Page 2 | 0x1000 – 0x1FFF |
| Page 3 | 0x2000 – 0x2FFF |

---

## Generating Hex Dump

Command:

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

## SQLite File Header Analysis

The first 100 bytes of every SQLite database contain metadata.

### Header Breakdown

| Offset | Value | Meaning |
|--------|------|---------|
| 0x00–0x0F | SQLite format 3 | SQLite signature |
| 0x10–0x11 | 1000 | Page size = 4096 bytes |
| 0x12 | 01 | Write version |
| 0x13 | 01 | Read version |
| 0x1C–0x1F | 00000003 | Database size in pages |

Observation:
This confirms the PRAGMA output.

---

## Finding Table Schema in Binary

Using:

```bash
strings library.db
```

Output included:

```text
tableauthorsauthors
CREATE TABLE authors

tablebooksbooks
CREATE TABLE books
```

This shows SQLite stores schema definitions inside the database file.

---

## Root Page Identification

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

The schema entries indicate:

- books table root page = Page 2
- authors table root page = Page 3

---

## SQLite B-Tree Overview

SQLite stores tables using B-tree pages.

Common page types:

| Hex Value | Meaning |
|---------|---------|
| 02 | Interior Index Page |
| 05 | Interior Table Page |
| 0A | Leaf Index Page |
| 0D | Leaf Table Page |

In this database, both data pages are leaf table pages.

---

## Page 2 Analysis (Books Table)

Page 2 begins at:

```text
0x1000
```

Command:

```bash
xxd -s 0x1000 library.db | head
```

Output:

```text
00001000: 0d00 0000 050f 7900 0fd6 0fc4 0fae 0f90
```

### Interpretation

| Offset | Value | Meaning |
|--------|------|---------|
| 0x1000 | 0D | Leaf table B-tree page |
| 0x1003–0x1004 | 0005 | Number of rows |
| 0x1005–0x1006 | 0F79 | Start of content area |

Cell pointers:

```text
0FD6
0FC4
0FAE
0F90
0F79
```

Meaning:
These pointers point to actual book records stored near the end of the page.

---

## Actual Book Record

Search:

```bash
grep -i "Dune" dump.txt
```

Output:

```text
00001fc0: ... Dune ...
```

This confirms the actual record payload exists physically inside the database file.

SQLite stores text values directly in payload sections.

---

## Page 3 Analysis (Authors Table)

Page 3 begins at:

```text
0x2000
```

Command:

```bash
xxd -s 0x2000 library.db | head
```

Output:

```text
00002000: 0d00 0000 030f b300 0fde 0fc8 0fb3
```

### Interpretation

| Offset | Value | Meaning |
|--------|------|---------|
| 0x2000 | 0D | Leaf table page |
| 0x2003–0x2004 | 0003 | Number of rows |
| 0x2005–0x2006 | 0FB3 | Start of content area |

Cell pointers:

```text
0FDE
0FC8
0FB3
```

These correspond to author records.

---

## Actual Author Record

Search:

```bash
grep -i "Orwell" dump.txt
```

Output:

```text
00002fc0: OrwellUK
```

This confirms author data is also stored directly inside payload sections.

---

## Record Storage Structure

A SQLite record generally contains:

- payload length
- row ID
- record header
- serial type information
- actual field data

Example:

For the book record:

```text
Dune | SciFi | 412
```

SQLite stores:
- text strings
- integer values
- metadata describing field types

---

## B-Tree Lookup Process

Example query:

```sql
SELECT * FROM books WHERE id = 2;
```

SQLite likely performs:

1. Open root page (Page 2)
2. Read page header
3. Check cell pointer array
4. Compare row IDs
5. Locate matching record
6. Read payload
7. Convert binary data into table row

---

## Overall File Layout

```text
SQLite Database File
│
├── Database Header (100 bytes)
│
├── Page 1
│   ├── SQLite internal schema
│   ├── books schema
│   └── authors schema
│
├── Page 2
│   ├── books B-tree page
│   ├── cell pointer array
│   └── book records
│
└── Page 3
    ├── authors B-tree page
    ├── cell pointer array
    └── author records
```

---

## Conclusion

This experiment helped in understanding how SQLite organizes data internally.

Key observations:

- SQLite stores everything in fixed-size pages.
- Database metadata is stored in the first 100 bytes.
- Table schemas are stored inside the database file itself.
- Tables are organized as B-tree structures.
- Cell pointers help locate actual records.
- Actual text data can be seen directly in the hex dump.

Using `xxd` made it easier to connect logical SQL tables with physical binary storage.