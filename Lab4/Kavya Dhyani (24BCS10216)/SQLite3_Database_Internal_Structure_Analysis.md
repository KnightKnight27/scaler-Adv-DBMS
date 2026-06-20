# SQLite3 Database Internal Structure Analysis using XXD

## Objective

The objective of this lab was to analyze the internal structure of a SQLite3 database file using hexadecimal dumps generated with the `xxd` utility. The experiment demonstrates how SQLite stores:

- Database headers
- B-tree pages
- Table records
- Index structures
- Metadata
- Cell pointers
- Record payloads

The analysis was performed on a custom `students.db` database.

---

# Database Creation

The database was created using SQLite3.

## Schema

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

---

# Database Records

The table contains 12 student records.

Example rows:

| First Name | Last Name | Age | Course           |
| ---------- | --------- | --- | ---------------- |
| Kartik     | Bhatia    | 22  | Computer Science |
| Prashansa  | Sharma    | 21  | Electronics      |
| Rahul      | Joshi     | 24  | Computer Science |
| Ananya     | Das       | 20  | Civil            |

---

# SQLite Database Metadata

The following commands were used:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT name, rootpage
FROM sqlite_master;
```

## Output

```text
Page Size  : 4096 bytes
Page Count : 4
```

## Root Pages

| Object                      | Root Page |
| --------------------------- | --------- |
| students                    | 2         |
| sqlite_autoindex_students_1 | 3         |
| sqlite_autoindex_students_2 | 4         |

---

# Physical File Layout

Since each page is 4096 bytes:

| Page Number | File Offset |
| ----------- | ----------- |
| Page 1      | 0x0000      |
| Page 2      | 0x1000      |
| Page 3      | 0x2000      |
| Page 4      | 0x3000      |

---

# SQLite File Header Analysis

The beginning of the database file was inspected using:

```bash
xxd -g 1 -l 128 students.db
```

## Hex Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

## ASCII Decoding

```text
SQLite format 3
```

This is the SQLite magic header that identifies the file as a valid SQLite3 database.

---

# SQLite Header Breakdown

| Bytes                | Meaning |
| -------------------- | ------- |
| 53 51 4C 69          | SQLi    |
| 74 65                | te      |
| 20 66 6F 72 6D 61 74 | format  |
| 20 33 00             | 3       |

---

# Page Size Analysis

Bytes:

```text
10 00
```

Hex:

```text
0x1000 = 4096
```

This matches the output of:

```sql
PRAGMA page_size;
```

---

# SQLite B-Tree Structure

SQLite internally stores all data using B-tree structures.

The database contains:

| Page | Type              | Purpose                |
| ---- | ----------------- | ---------------------- |
| 1    | Table B-tree      | sqlite_master metadata |
| 2    | Leaf Table B-tree | students table         |
| 3    | Leaf Index B-tree | UNIQUE email index     |
| 4    | Leaf Index B-tree | PRIMARY KEY index      |

---

# Overall Database Layout

```text
students.db
│
├── Page 1 (0x0000 – 0x0FFF)
│     ├── SQLite File Header
│     ├── sqlite_master B-tree
│     ├── Schema Records
│     └── CREATE TABLE statements
│
├── Page 2 (0x1000 – 0x1FFF)
│     ├── students Table B-tree
│     ├── Cell Pointer Array
│     └── Student Records
│
├── Page 3 (0x2000 – 0x2FFF)
│     └── UNIQUE Email Index B-tree
│
└── Page 4 (0x3000 – 0x3FFF)
      └── PRIMARY KEY Index B-tree
```

---

# Analysis of Page 2 (Students Table)

The students table root page is page 2.

Command used:

```bash
xxd -g 1 -s 4096 -l 512 students.db
```

## Beginning of Page 2

```text
0d 00 00 00 0c 0c 75 00
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

# Decoded Values

| Bytes | Value | Meaning                |
| ----- | ----- | ---------------------- |
| 0d    | 13    | Leaf Table B-tree Page |
| 00 00 | 0     | No freeblocks          |
| 00 0c | 12    | 12 records             |
| 0c 75 | 3189  | Cell content begins    |
| 00    | 0     | No fragmented bytes    |

---

# Cell Pointer Array

Immediately after the page header:

```text
0f b4
0f 67
0f 1e
0e cf
0e 8b
0e 3d
0d e8
0d 9c
0d 4d
0d 02
0c b7
0c 75
```

These are 12 cell pointers.

Each pointer stores the offset of a record inside the page.

---

# Example Pointer Analysis

Pointer:

```text
0f b4
```

Hex:

```text
0x0FB4 = 4020
```

Absolute file location:

```text
4096 + 4020 = 8116
```

Thus SQLite can directly locate the row payload using the pointer array.

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

This design minimizes memory movement during insertion and deletion.

---

# Record Payload Analysis

The record payload area begins at:

```text
0x0C75
```

Command used:

```bash
xxd -g 1 -s 7285 -l 512 students.db
```

---

# Extracted Record Data

The dump visibly contains real student data:

```text
41 6e 61 6e 79 61
```

ASCII:

```text
Ananya
```

---

# Email Field

Hex:

```text
61 6e 61 6e 79 61 2e 64 61 73 40 ...
```

ASCII:

```text
ananya.das@example.com
```

---

# Course Field

Hex:

```text
43 69 76 69 6c
```

ASCII:

```text
Civil
```

---

# Timestamp Field

Hex:

```text
32 30 32 36 2d 30 35 2d 31 33 ...
```

ASCII:

```text
2026-05-13 21:27:11
```

This demonstrates that SQLite stores timestamps as TEXT values by default.

---

# SQLite Record Format

SQLite records are stored in the following format:

```text
[Payload Size]
[Row ID]
[Record Header Size]
[Serial Types]
[Actual Column Data]
```

---

# Multiple Records in a Single Page

The dump shows multiple student records packed together:

* Ananya Das
* Vikram Patel
* Neha Agarwal
* Rahul Joshi
* Pooja Singh
* Aditya Nair
* Ishita Kapoor

This proves that SQLite stores multiple rows compactly inside a single leaf B-tree page.

---

# Analysis of sqlite_master

The sqlite_master table stores database metadata.

Hex dump:

```text
74 61 62 6c 65 73 74 75 64 65 6e 74 73
```

ASCII:

```text
tablestudents
```

---

# CREATE TABLE Statement Stored Internally

The dump also contains:

```text
CREATE TABLE students
```

This proves that SQLite stores schema definitions directly inside the database file.

---

# Root Page References Stored Internally

The dump contains references to:

| Object                      | Root Page |
| --------------------------- | --------- |
| students                    | 2         |
| sqlite_autoindex_students_1 | 3         |
| sqlite_autoindex_students_2 | 4         |

These match the output of:

```sql
SELECT name, rootpage FROM sqlite_master;
```

---

# Index B-Tree Pages

Pages 3 and 4 begin with:

```text
0a
```

Meaning:

| Hex | Meaning                |
| --- | ---------------------- |
| 0A  | Leaf Index B-tree Page |

Unlike table B-trees, index B-trees store:

* indexed keys
* row references

instead of full row payloads.

---

# Table B-Tree vs Index B-Tree

| Feature                | Table B-tree | Index B-tree    |
| ---------------------- | ------------ | --------------- |
| Stores full rows       | Yes          | No              |
| Stores indexed keys    | No           | Yes             |
| Stores payload data    | Yes          | Usually No      |
| Used for row retrieval | Yes          | Used for search |

---

# Lookup Process in SQLite

Example query:

```sql
SELECT * FROM students
WHERE email='rahul.joshi@example.com';
```

SQLite internally performs:

```text
Root Page
    ↓
Index B-tree (email index)
    ↓
Find matching key
    ↓
Retrieve row reference
    ↓
Navigate to table B-tree
    ↓
Locate cell pointer
    ↓
Read actual row payload
```

---

# Important Observations

1. SQLite stores everything using B-trees.
2. Metadata itself is stored as tables.
3. Records are variable-length.
4. Cell pointers provide fast lookup.
5. SQLite pages grow from opposite directions.
6. Table and index B-trees serve different purposes.
7. SQL schema definitions are physically stored inside the database file.
