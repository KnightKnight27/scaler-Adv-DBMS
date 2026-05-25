# SQLite3 Database Internal Structure Analysis using XXD

## Objective

The objective of this lab was to analyze the internal structure of a SQLite3 database file using hexadecimal dumps generated using the `xxd` utility. The experiment demonstrates how SQLite internally stores:

* SQLite database headers
* B-tree pages
* Table records
* Metadata
* Cell pointer arrays
* Record payloads
* Root page addresses
* Table lookup structures

A custom SQLite database named `lab.db` was created and inspected at the binary level using real hexadecimal dumps. 

---

# Database Creation

The database was created using SQLite3.

## Schema Used

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    dept TEXT
);
```

The schema was verified using:

```sql
.schema students
```

Output:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    dept TEXT
);
```



---

# Records Inserted

The table contains 10 student records.

## Table Contents

| ID | Name    | Age | Department |
| -- | ------- | --- | ---------- |
| 1  | Alice   | 20  | CSE        |
| 2  | Bob     | 21  | ECE        |
| 3  | Charlie | 22  | MECH       |
| 4  | David   | 20  | EEE        |
| 5  | Eva     | 23  | CIVIL      |
| 6  | Frank   | 24  | CSE        |
| 7  | Grace   | 21  | ECE        |
| 8  | Helen   | 22  | MECH       |
| 9  | Ian     | 20  | EEE        |
| 10 | Jane    | 23  | CIVIL      |

Verified using:

```sql
SELECT * FROM students;
```



---

# SQLite Database Metadata

The following commands were executed:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT rootpage, sql
FROM sqlite_master
WHERE type='table';
```

---

# Metadata Output

```text
Page Size  : 4096 bytes
Page Count : 2
```

The `students` table root page:

| Table Name | Root Page |
| ---------- | --------- |
| students   | 2         |



---

# Physical File Layout

Since each SQLite page is 4096 bytes:

| Page Number | Offset Range    |
| ----------- | --------------- |
| Page 1      | 0x0000 – 0x0FFF |
| Page 2      | 0x1000 – 0x1FFF |

---

# SQLite File Header Analysis

The database file was inspected using:

```bash
xxd -g 1 lab.db
```

Beginning of the dump:

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```



---

# ASCII Decoding

The hexadecimal bytes decode to:

```text
SQLite format 3
```

This is the SQLite magic signature used to identify a valid SQLite3 database file.

---

# SQLite Header Breakdown

| Hex Bytes            | Meaning |
| -------------------- | ------- |
| 53 51 4C 69          | SQLi    |
| 74 65                | te      |
| 20 66 6F 72 6D 61 74 | format  |
| 20 33 00             | 3       |

---

# Page Size Analysis

Bytes at offset `0x10`:

```text
10 00
```

Hexadecimal:

```text
0x1000 = 4096
```

This matches the SQL output from:

```sql
PRAGMA page_size;
```



---

# SQLite Internal B-Tree Structure

SQLite stores all tables and metadata internally using B-tree pages.

The database contains:

| Page | Type              | Purpose                |
| ---- | ----------------- | ---------------------- |
| 1    | Table B-tree      | sqlite_master metadata |
| 2    | Leaf Table B-tree | students table         |

---

# Overall Database Layout

```text
lab.db
│
├── Page 1 (0x0000 – 0x0FFF)
│     ├── SQLite File Header
│     ├── sqlite_master B-tree
│     ├── Table Metadata
│     └── CREATE TABLE statement
│
└── Page 2 (0x1000 – 0x1FFF)
      ├── students Table B-tree
      ├── Cell Pointer Array
      └── Student Records
```

---

# Analysis of Page 1 (sqlite_master)

At offset `0x0000`, the database header is stored.

At offset `0x0060`, the B-tree page for `sqlite_master` begins:

```text
00000060: 00 2e 8d f8 0d 00 00 00 01 0f 71 00 0f 71 00 00
```



---

# Decoding the sqlite_master Page Header

Important bytes:

```text
0d 00 00 00 01 0f 71 00
```

| Bytes | Meaning                            |
| ----- | ---------------------------------- |
| 0d    | Leaf Table B-tree Page             |
| 00 00 | No freeblocks                      |
| 00 01 | 1 cell                             |
| 0f 71 | Cell content begins at offset 3953 |
| 00    | No fragmented bytes                |

This page contains the schema definition record.

---

# CREATE TABLE Statement Stored Internally

The schema text is physically visible inside the database dump:

```text
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    dept TEXT
)
```

Visible in hex dump:

```text
43 52 45 41 54 45 20 54 41 42 4c 45
```

ASCII:

```text
CREATE TABLE
```



---

# Analysis of Page 2 (Students Table)

Page 2 begins at file offset:

```text
0x1000
```

Beginning of Page 2:

```text
00001000: 0d 00 00 00 0a 0f 53 00
```



---

# Decoding the Page Header

SQLite leaf table page header format:

| Offset | Size    | Meaning               |
| ------ | ------- | --------------------- |
| 0      | 1 byte  | Page Type             |
| 1-2    | 2 bytes | First Freeblock       |
| 3-4    | 2 bytes | Number of Cells       |
| 5-6    | 2 bytes | Start of Cell Content |
| 7      | 1 byte  | Fragmented Free Bytes |

---

# Decoded Header Values

| Bytes | Value | Meaning                |
| ----- | ----- | ---------------------- |
| 0d    | 13    | Leaf Table B-tree Page |
| 00 00 | 0     | No freeblocks          |
| 00 0a | 10    | 10 table records       |
| 0f 53 | 3923  | Cell content begins    |
| 00    | 0     | No fragmented bytes    |

This confirms the students table contains exactly 10 rows.

---

# Cell Pointer Array

Immediately after the header:

```text
0f e4
0f d6
0f c3
0f b3
0f a3
0f 93
0f 83
0f 72
0f 64
0f 53
```



These are cell pointers.

Each pointer stores the location of a record inside the page.

---

# Pointer Interpretation

Example pointer:

```text
0f e4
```

Hexadecimal:

```text
0x0FE4 = 4068
```

Absolute file address:

```text
4096 + 4068 = 8164
```

SQLite directly jumps to this offset to read the row payload.

---

# SQLite Page Organization

SQLite pages grow in opposite directions:

```text
+----------------------+
| B-tree Page Header   |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
|                      |
|                      |
+----------------------+
| Record Data          |
| (grows upward)       |
+----------------------+
```

This structure minimizes memory movement during insertion operations.

---

# Record Payload Analysis

The records are stored near the end of the page.

Example extracted text from the dump:

```text
41 6c 69 63 65
```

ASCII:

```text
Alice
```

---

# Additional Record Examples

## Department Field

Hex:

```text
43 53 45
```

ASCII:

```text
CSE
```

---

## Another Record

Hex:

```text
43 68 61 72 6c 69 65
```

ASCII:

```text
Charlie
```

---

## Department Example

Hex:

```text
4d 45 43 48
```

ASCII:

```text
MECH
```

These values confirm that SQLite stores actual record payloads directly inside the B-tree leaf page.

---

# SQLite Record Format

SQLite records follow this structure:

```text
[Payload Size]
[Row ID]
[Record Header Size]
[Serial Types]
[Column Data]
```

Each row is variable length depending on field sizes.

---

# sqlite_master Metadata

The dump contains:

```text
74 61 62 6c 65 73 74 75 64 65 6e 74 73
```

ASCII:

```text
tablestudents
```

This confirms that SQLite stores metadata as records inside internal tables.

---

# Root Page Reference

The metadata references:

| Object   | Root Page |
| -------- | --------- |
| students | 2         |

This matches the SQL query:

```sql
SELECT rootpage, sql
FROM sqlite_master
WHERE type='table';
```



---

# Lookup Process in SQLite

Example query:

```sql
SELECT * FROM students
WHERE id = 5;
```

SQLite internally performs:

```text
Root Page
    ↓
Students Table B-tree (Page 2)
    ↓
Read Cell Pointer Array
    ↓
Locate matching row
    ↓
Read record payload
```

---

# Important Observations

1. SQLite stores all data using B-tree structures.
2. Database metadata is stored inside normal database pages.
3. Schema definitions are physically stored inside the file.
4. Table rows are variable length.
5. Cell pointers allow efficient row lookup.
6. SQLite pages grow from opposite directions.
7. The database file can be fully inspected using hexadecimal tools.
8. SQLite requires only two pages to store both metadata and 10 student records in this example.

---

# Conclusion

This experiment successfully demonstrated the internal physical structure of a SQLite3 database using `xxd` hexadecimal dumps.

The analysis showed:

* SQLite file headers
* Database page organization
* Leaf table B-tree pages
* Cell pointer arrays
* Root page references
* Record payload storage
* Internal schema storage
* Record lookup navigation

The experiment proves that SQLite is a highly compact self-contained database engine that stores all information using B-tree pages and variable-length records inside a single binary file.
