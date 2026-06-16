# Lab 4 - SQLite3 Hex Dump and B-Tree Analysis

## Objective

The objective of this lab is to study the internal storage structure of a SQLite3 database file using a hex dump generated with `xxd`. The lab focuses on understanding SQLite pages, B-tree structures, page headers, cell pointers, and how records are stored internally.

---

## Files Submitted

| File | Description |
|------|-------------|
| `campus.db` | SQLite database created for the lab |
| `create_campus.sql` | SQL script used to create tables and insert records |
| `campus.hex` | Hex dump generated using `xxd` |
| `README.md` | Documentation and analysis of SQLite internal structure |

---

## Database Creation

The following SQL script was used to create the database and insert sample data.

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    grade INTEGER,
    department TEXT
);

CREATE INDEX idx_students_grade
ON students(grade);

INSERT INTO students (name, grade, department) VALUES
('Alice', 91, 'CSE'),
('Bob', 85, 'ISE'),
('Carol', 88, 'ECE'),
('David', 76, 'ME'),
('Eva', 95, 'CSE'),
('Frank', 82, 'EEE'),
('Grace', 89, 'ISE'),
('Helen', 92, 'ECE'),
('Ian', 80, 'CSE'),
('Jack', 87, 'ME'),
('Kevin', 78, 'EEE'),
('Luna', 90, 'CSE'),
('Mia', 84, 'ISE'),
('Noah', 93, 'ECE'),
('Olivia', 86, 'ME');

VACUUM;
```

---

## Commands Used

### Create Database

```bash
/c/sqlite/sqlite3.exe Lab4/campus.db < Lab4/create_campus.sql
```

### Generate Hex Dump

```bash
xxd -g 1 Lab4/campus.db > Lab4/campus.hex
```

### View Database Information

```bash
/c/sqlite/sqlite3.exe Lab4/campus.db ".dbinfo"
```

### View Schema Information

```bash
/c/sqlite/sqlite3.exe Lab4/campus.db "SELECT type,name,rootpage,sql FROM sqlite_schema;"
```

---

## SQLite File Structure

SQLite stores the complete database inside a single binary file divided into fixed-size pages.

The default page size for this database is:

```text
4096 bytes
```

Each page begins at:

```text
(page_number - 1) × 4096
```

| Page Number | File Offset |
|-------------|-------------|
| Page 1 | `0x0000` |
| Page 2 | `0x1000` |
| Page 3 | `0x2000` |

---

## SQLite File Header

The first 100 bytes of the database file contain the SQLite database header.

### Hex Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

The ASCII representation of the above bytes is:

```text
SQLite format 3
```

This confirms that the file is a valid SQLite3 database.

### Important Header Fields

| Offset | Value | Meaning |
|--------|-------|---------|
| `0x00` | `SQLite format 3` | SQLite signature |
| `0x10` | `10 00` | Page size = 4096 bytes |
| `0x1C` | Page count | Number of pages in database |
| `0x38` | `00 00 00 01` | UTF-8 text encoding |

---

## SQLite B-Tree Structure

SQLite internally stores tables and indexes using B-tree structures.

### Common B-Tree Page Types

| Hex Value | Meaning |
|-----------|---------|
| `0x0D` | Table Leaf Node |
| `0x0A` | Index Leaf Node |
| `0x05` | Interior Table Node |
| `0x02` | Interior Index Node |

---

## Page 1 - sqlite_schema Table

Page 1 contains the database header and the `sqlite_schema` table.

The schema table stores information about:
- tables
- indexes
- root pages
- SQL definitions

### Example Schema Query

```sql
SELECT type,name,rootpage,sql FROM sqlite_schema;
```

The output shows:
- `students` table
- `idx_students_grade` index

The root page values point to the pages where the actual B-tree structures are stored.

---

## Page 2 - Students Table B-Tree

Page 2 stores the `students` table records.

### Page Header

```text
00001000: 0d 00 00 00 ...
```

The byte `0d` indicates that this page is a:

```text
Table Leaf B-Tree Page
```

### Cell Pointer Array

The page header contains pointers to individual row records stored inside the page.

Each pointer stores the offset of a row cell relative to the start of the page.

### Row Storage

The actual row data is stored near the bottom of the page.

ASCII text such as student names can be identified directly in the hex dump.

Example:

```text
Alice
Bob
Carol
```

Each row contains:
- rowid
- record header
- column values

---

## Page 3 - Index B-Tree

Page 3 stores the index created on the `grade` column.

### Page Header

```text
00002000: 0a 00 00 00 ...
```

The byte `0a` represents:

```text
Index Leaf B-Tree Page
```

### Index Structure

The index stores:
- grade value
- corresponding rowid

The index helps SQLite quickly locate matching rows without scanning the complete table.

---

## Lookup Example

### Query

```sql
SELECT * FROM students WHERE grade = 91;
```

### Lookup Process

1. SQLite first searches the index B-tree on the `grade` column.
2. The index returns the matching rowid.
3. SQLite then accesses the table B-tree to retrieve the full row data.

This reduces the number of page scans and improves lookup performance.

---

## Navigation Using Offsets

The hex dump can be navigated using file offsets.

Examples:

| Offset | Description |
|--------|-------------|
| `00000000` | SQLite file header |
| `00001000` | Start of Page 2 |
| `00002000` | Start of Page 3 |

This makes it possible to manually identify page boundaries and B-tree nodes from the dump.

---

## Observations

- SQLite stores the entire database inside a single binary file.
- Tables and indexes are implemented internally using B-trees.
- Row records are stored inside leaf pages.
- Cell pointer arrays help SQLite quickly locate records inside a page.
- Index B-trees improve lookup efficiency by reducing full table scans.

---

## Conclusion

This lab provided practical understanding of the SQLite3 on-disk file format and B-tree implementation. By examining the database using `xxd`, it was possible to observe how SQLite organizes pages, stores records, maintains indexes, and performs lookups internally using B-tree structures.