# Lab 4 - SQLite3 Storage and B-Tree Exploration

## Aim

The purpose of this experiment is to understand how SQLite stores data internally inside a database file.  
Using a hexadecimal dump generated through `xxd`, the lab studies SQLite page organization, B-Tree layouts, page headers, record storage, and indexing mechanisms.

---

# Database Setup

The following SQL script was used to create the database structure and insert sample records.

```sql
CREATE TABLE learners (
    student_id INTEGER PRIMARY KEY,
    full_name TEXT NOT NULL,
    marks INTEGER,
    branch TEXT
);

CREATE INDEX idx_learners_marks
ON learners(marks);

INSERT INTO learners (full_name, marks, branch) VALUES
('Arjun', 90, 'CSE'),
('Bhavna', 84, 'ISE'),
('Charan', 87, 'ECE'),
('Deepa', 75, 'ME'),
('Farhan', 96, 'CSE'),
('Gauri', 81, 'EEE'),
('Harsha', 88, 'ISE'),
('Ishita', 93, 'ECE'),
('Jatin', 79, 'CSE'),
('Kiran', 86, 'ME'),
('Lavanya', 77, 'EEE'),
('Manoj', 91, 'CSE'),
('Nisha', 83, 'ISE'),
('Omkar', 94, 'ECE'),
('Pooja', 85, 'ME');

VACUUM;
```

---

# Commands Executed

## Create Database

```bash
sqlite3 campus.db < create_campus.sql
```

## Generate Hex Dump

```bash
xxd -g 1 campus.db > campus.hex
```

## View Database Information

```bash
sqlite3 campus.db ".dbinfo"
```

## View Schema Details

```bash
sqlite3 campus.db "SELECT type,name,rootpage,sql FROM sqlite_schema;"
```

---

# SQLite File Organization

SQLite stores the complete database in a single binary file divided into fixed-size pages.

Default page size:

```text
4096 bytes
```

Page offset calculation:

```text
(page_number - 1) × 4096
```

| Page Number | Offset | Purpose |
|-------------|--------|----------|
| Page 1 | `0x0000` | Database Header + Schema Table |
| Page 2 | `0x1000` | Table Leaf Page (`learners`) |
| Page 3 | `0x2000` | Index Leaf Page (`idx_learners_marks`) |

> **Note:**  
> Some SQLite builds reserve a few bytes at the end of every page for internal metadata.

---

# SQLite Database Header

The first 100 bytes of the file contain the SQLite database header.

Example from hex dump:

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

This corresponds to:

```text
SQLite format 3
```

Important header fields:

| Offset | Meaning |
|--------|----------|
| `0x10 - 0x11` | Page size |
| `0x12` | Write version |
| `0x13` | Read version |
| `0x1C - 0x1F` | Total number of pages |
| `0x38 - 0x3B` | Text encoding |

---

# SQLite B-Tree Page Types

SQLite uses B-Trees internally for storing tables and indexes.

| Hex Value | Page Type |
|-----------|------------|
| `0x0D` | Table Leaf Page |
| `0x0A` | Index Leaf Page |
| `0x05` | Table Interior Page |
| `0x02` | Index Interior Page |

---

# Page 1 - Schema Table

Page 1 stores the SQLite schema information.

Example page header:

```text
00000060: 00 2e 8d f8 0d 00 00 00
```

Important observations:

- `0d` indicates a Table Leaf Page
- Page contains metadata about tables and indexes
- Cell pointer array stores offsets of schema records

---

# Page 2 - Learners Table

Page 2 contains all table records for the `learners` table.

Example page header:

```text
00001000: 0d 00 00 00 0f 0f 11 00
```

Observations:

- `0d` → Table Leaf Page
- `0f` → 15 records inserted
- Cell pointers are arranged in ascending RowID order

SQLite stores records from the bottom of the page upward while cell pointers grow downward from the top.

---

# Example Record Decoding

Sample learner record:

```text
00001fe0: ...
```

Decoded information:

| Field | Value |
|------|-------|
| RowID | 1 |
| Name | Arjun |
| Marks | 90 |
| Branch | CSE |

SQLite uses serial types to identify column data types and sizes.

---

# Page 3 - Index B-Tree

Page 3 stores index entries for `marks`.

Example page header:

```text
00002000: 0a 00 00 00 0f 0f 9b 00
```

Observations:

- `0a` indicates Index Leaf Page
- Entries are sorted by `marks`
- Each index record stores:
  - indexed value
  - associated RowID

This improves lookup efficiency.

---

# Query Lookup Example

```sql
SELECT * FROM learners WHERE marks = 90;
```

### Lookup Process

1. SQLite first searches the index B-Tree.
2. Matching marks value is located using binary search.
3. Corresponding RowID is extracted.
4. SQLite accesses the table B-Tree using the RowID.
5. Final learner record is returned.

---

# Key Observations

- SQLite stores everything inside fixed-size pages.
- B-Trees are used for fast searching and indexing.
- Cell pointer arrays allow efficient binary search.
- `VACUUM` reorganizes and optimizes storage.
- Variable-length integers reduce storage usage.
- RowIDs act as internal primary keys.

---

# Conclusion

This experiment provided an understanding of SQLite’s internal storage architecture using hexadecimal analysis.

Through the hex dump and B-Tree inspection, it became clear how SQLite organizes pages, stores records, manages indexes, and performs efficient searches while minimizing storage overhead.