# SQLite3 Database Internal Structure Analysis using XXD

## Objective

The objective of this lab was to analyze the internal structure of a SQLite3 database file using hexadecimal dumps generated using the `xxd` utility. The experiment demonstrates how SQLite internally stores:

- Database headers
- B-tree pages
- Table records
- Metadata
- Cell pointer arrays
- Record payloads
- Schema definitions

The analysis was performed on a custom `students` database.

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

The table contains student records.

Example rows:

| First Name | Last Name | Age | Email                                                 | Course           | Created At          |
| ---------- | --------- | --- | ----------------------------------------------------- | ---------------- | ------------------- |
| Kartik     | Bhatia    | 22  | [kartik@example.com](mailto:kartik@example.com)       | Computer Science | 2026-05-13 21:27:11 |
| Prashansa  | Sharma    | 21  | [prashansa@example.com](mailto:prashansa@example.com) | Electronics      | 2026-05-13 21:27:11 |

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
xxd -g 1 -l 512 students.db
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
| 3    | Index B-tree      | Auto index             |
| 4    | Index B-tree      | Auto index             |

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
│     └── Auto Index B-tree
│
└── Page 4 (0x3000 – 0x3FFF)
      └── Auto Index B-tree
```

---

# Analysis of Page 2 (Students Table)

The `students` table root page is page 2.

Command used:

```bash
xxd -g 1 -s 4096 -l 512 students.db
```

## Beginning of Page 2

```text
0d 00 00 00 02 0f 67 00
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
| 00 02 | 2     | 2 records              |
| 0f 67 | 3943  | Cell content begins    |
| 00    | 0     | No fragmented bytes    |

---

# Cell Pointer Array

Immediately after the page header:

```text
0f b4
0f 67
```

These are cell pointers.

Each pointer stores the offset of a record inside the page.

---

# Example Pointer Analysis

Pointer:

```text
0f 67
```

Hex:

```text
0x0F67 = 3943
```

Absolute file location:

```text
4096 + 3943 = 8039
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
0x0F67
```

Command used:

```bash
xxd -g 1 -s 8039 -l 512 students.db
```

---

# Extracted Record Data

The dump visibly contains real student data.

## Student First Name

Hex:

```text
4b 61 72 74 69 6b
```

ASCII:

```text
Kartik
```

---

# Student Last Name

Hex:

```text
42 68 61 74 69 61
```

ASCII:

```text
Bhatia
```

---

# Email Field

Hex:

```text
6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d
```

ASCII:

```text
kartik@example.com
```

---

# Course Field

Hex:

```text
43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63 65
```

ASCII:

```text
Computer Science
```

---

# Additional Extracted Records

The dump also contains:

| Hex ASCII                                             | Decoded Text |
| ----------------------------------------------------- | ------------ |
| Prashansa                                             | Student Name |
| Sharma                                                | Last Name    |
| [prashansa@example.com](mailto:prashansa@example.com) | Email        |
| Electronics                                           | Course       |
| 2026-05-13 21:27:11                                   | Timestamp    |

This proves that SQLite stores multiple rows compactly inside a single leaf B-tree page.

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

SQLite uses variable-length encoding to optimize storage efficiency.

---

# Analysis of sqlite_master

The `sqlite_master` table stores database metadata.

The database internally stores:

- Table definitions
- Root page references
- Schema metadata
- Index definitions

---

# CREATE TABLE Statement Stored Internally

The output of:

```bash
strings students.db
```

contains:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
```

This proves that SQLite stores schema definitions directly inside the database file.

---

# Root Page References Stored Internally

The database internally stores references to:

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

# Important Observations

1. SQLite stores everything using B-trees.
2. Metadata itself is stored as tables.
3. Records are variable-length.
4. Cell pointers provide fast lookup.
5. SQLite pages grow from opposite directions.
6. Multiple records can exist compactly within a single page.
7. SQL schema definitions are physically stored inside the database file.
8. SQLite automatically created index pages for UNIQUE constraints.
9. The database file can be analyzed directly using hexadecimal tools like `xxd`.
10. SQLite uses type affinity instead of strict typing like PostgreSQL.

---

# Conclusion

This experiment successfully demonstrated the low-level internal structure of a SQLite3 database file. Using hexadecimal analysis with `xxd`, it was possible to inspect database headers, B-tree structures, metadata pages, record payloads, and schema definitions stored physically inside the database file.

The analysis proves that SQLite is a self-contained database engine that stores all tables, indexes, metadata, and records directly inside a compact binary file structure organized using B-trees.
