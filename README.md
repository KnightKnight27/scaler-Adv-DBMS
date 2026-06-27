# Lab 4 - SQLite Hex & B-Tree Analysis

## Objective

Understand how SQLite organises tables and indexes internally as B-Tree pages by examining the raw binary layout of a database file.

---

## Files

| File | Description |
|------|-------------|
| `create_campus.sql` | SQL schema, index, and 15 sample student records |
| `campus.db` | SQLite database file (3 pages × 4 096 B = 12 KB) |
| `campus.hex` | Full `xxd` hex dump of `campus.db` |
| `analyse.py` | Python script that decodes the binary file header, page headers, cell pointer arrays, and all rows — no external libs |

---

## Database Schema

```sql
CREATE TABLE students (
    id         INTEGER PRIMARY KEY,   -- rowid alias; B-Tree key
    name       TEXT    NOT NULL,
    grade      INTEGER,
    department TEXT
);

CREATE INDEX idx_students_grade ON students(grade);
```

---

## Internal B-Tree Layout (3 Pages)

```
campus.db  (12 288 bytes, page size = 4 096 bytes)
│
├── Page 1  [Leaf Table B-Tree]  — sqlite_schema (system catalog)
│    ├── Cell 0 → row: type='table', name='students',  rootpage=2
│    └── Cell 1 → row: type='index', name='idx_students_grade', rootpage=3
│
├── Page 2  [Leaf Table B-Tree]  — students table data
│    └── 15 cells, one per student row, ordered by rowid (= id)
│
└── Page 3  [Leaf Index B-Tree]  — idx_students_grade index
     └── 15 cells, each holding (grade, rowid) pairs in sorted order
```

---

## SQLite File Header (bytes 0–99)

| Offset | Size | Value | Meaning |
|--------|------|-------|---------|
| 0      | 16   | `SQLite format 3\0` | Magic string |
| 16     | 2    | 4 096 | Page size in bytes |
| 18     | 1    | 1     | File format write version |
| 19     | 1    | 1     | File format read version |
| 28     | 4    | 3     | Total number of pages |
| 40     | 4    | 3     | Schema cookie (increments on schema change) |
| 96     | 4    | 3051000 | SQLite library version number |

---

## B-Tree Page Header (bytes 0–7 of each page, or bytes 100–107 of page 1)

| Offset | Size | Meaning |
|--------|------|---------|
| 0      | 1    | Page type: `0x0D` = Leaf Table, `0x0A` = Leaf Index |
| 1      | 2    | Offset of first freeblock (0 = none) |
| 3      | 2    | Number of cells on this page |
| 5      | 2    | Offset to start of cell content area |
| 7      | 1    | Number of fragmented free bytes |

---

## Cell Pointer Array

Immediately after the page header. Each entry is a **2-byte big-endian offset** pointing into the cell content area of the same page. Cells are stored from the **end of the page upward**, so:

- Pointers go from highest-numbered (first inserted) down to the lowest offset (last inserted).
- Page 2 has 15 pointers: `[4080, 4066, 4050, ...]`

---

## Sample Decoded Rows (Page 2)

| id | name   | grade | dept |
|----|--------|-------|------|
| 1  | Alice  | 91    | CSE  |
| 2  | Bob    | 85    | ISE  |
| 3  | Carol  | 88    | ECE  |
| 4  | David  | 76    | ME   |
| 5  | Eva    | 95    | CSE  |
| …  | …      | …     | …    |
| 15 | Olivia | 86    | ME   |

---

## How to Run the Analysis

```bash
# Recreate the database
sqlite3 campus.db < create_campus.sql

# Regenerate hex dump
xxd campus.db > campus.hex

# Run the B-Tree page analyser
python3 analyse.py
```

---

## Key Learning Outcomes

| Concept | Observation |
|---------|-------------|
| B-Tree as storage | Every SQLite table is a B-Tree; rows are leaf-page cells keyed by rowid |
| INTEGER PRIMARY KEY | Acts as a rowid alias — stored as NULL in the record, value comes from rowid |
| Index as separate B-Tree | `idx_students_grade` occupies its own page (page 3), sorted by grade |
| Cell pointer array | Allows O(1) access to any cell; cells grow from page end inward |
| VACUUM | Compacts the file and removes fragmentation for cleaner hex analysis |
| Hex-level inspection | Magic header, page type byte, cell counts are all visible in `campus.hex` |
