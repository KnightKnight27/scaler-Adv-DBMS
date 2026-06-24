# SQLite3 Hex Dump and B-Tree Structure Analysis

## Objective

The objective of this lab was to:

* Create a real SQLite3 database
* Inspect the database using `xxd`
* Analyze the SQLite file format
* Decode SQLite pages and B-tree structures
* Understand SQLite internal storage
* Trace how SQLite navigates tables internally
* Decode actual table rows directly from raw hexadecimal bytes

---

# 1. Database Creation

## Create Database

The following commands were used to create the database.

```bash
sqlite3 lab.db
```

Inside the SQLite shell:

```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);

INSERT INTO students(name, age) VALUES
('Alice', 21),
('Bob', 22),
('Charlie', 20),
('David', 23),
('Eve', 24);

.quit
```

---

# 2. Verify Database File

Command:

```bash
ls -lh lab.db
```

Output:

```bash
-rw-r--r-- 1 aadi-gupta aadi-gupta 8.0K May 25 23:05 lab.db
```

Observation:

* Database size = 8 KB
* SQLite page size later confirms:

  * 2 pages
  * 4096 bytes each
  * Total = 8192 bytes

---

# 3. SQLite Database Header Analysis

## Hex Dump of Database Header

Command:

```bash
xxd lab.db | head -40
```

Output:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
00000010: 1000 0101 0040 2020 0000 0002 0000 0002
00000020: 0000 0000 0000 0000 0000 0001 0000 0004
00000030: 0000 0000 0000 0000 0000 0001 0000 0000
00000040: 0000 0000 0000 0000 0000 0000 0000 0000
00000050: 0000 0000 0000 0000 0000 0000 0000 0002
00000060: 002e 7689 0d00 0000 010f 8e00 0f8e 0000
```

---

# 4. SQLite File Header Fields

## SQLite Signature

Offset:

```text
0x00 – 0x0F
```

Bytes:

```text
53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII:

```text
SQLite format 3\0
```

Meaning:

* Confirms SQLite3 database format

---

## Page Size

Offset:

```text
0x10 – 0x11
```

Bytes:

```text
10 00
```

Hex:

```text
0x1000
```

Decimal:

```text
4096
```

Meaning:

* SQLite page size = 4096 bytes

---

## Number of Pages

Offset:

```text
0x1C – 0x1F
```

Bytes:

```text
00 00 00 02
```

Meaning:

* Database contains 2 pages

Calculation:

```text
4096 × 2 = 8192 bytes
```

Matches actual file size:

```text
8 KB
```

---

# 5. SQLite Page Layout

## Database Structure

| Page Number | Purpose                       |
| ----------- | ----------------------------- |
| Page 1      | SQLite header + sqlite_master |
| Page 2      | students table B-tree         |

---

# 6. SQLite B-Tree Page Structure

SQLite stores table data using B-tree pages.

## Generic B-tree Page Layout

```text
+---------------------------+
| B-tree Page Header        |
+---------------------------+
| Cell Pointer Array        |
+---------------------------+
|                           |
|         Free Space        |
|                           |
+---------------------------+
| Cell Content Area         |
| (grows backward upward)   |
+---------------------------+
```

Important observations:

* Pointer array grows downward
* Cell content grows upward from end of page
* This minimizes fragmentation

---

# 7. sqlite_master Analysis

The `sqlite_master` table stores metadata about all database objects.

Command:

```bash
sqlite3 lab.db "SELECT * FROM sqlite_master;"
```

Output:

```text
table|students|students|2|CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
)
```

Important observation:

| Field    | Meaning          |
| -------- | ---------------- |
| table    | Object type      |
| students | Table name       |
| 2        | Root page number |

Meaning:

* SQLite stores the root page number of every table
* `students` table root page = 2

---

# 8. Page 1 B-Tree Header

Page 1 B-tree begins at offset:

```text
0x64
```

because the first 100 bytes are reserved for the SQLite database header.

Hex:

```text
0d00 0000 010f 8e00 0f8e
```

Decoded:

| Bytes  | Meaning                |
| ------ | ---------------------- |
| `0d`   | Leaf table B-tree page |
| `0000` | No freeblocks          |
| `0001` | One cell               |
| `0f8e` | Start of cell content  |
| `00`   | No fragmented bytes    |
| `0f8e` | Cell pointer           |

Observation:

* Page contains one record
* That record is the `sqlite_master` entry

---

# 9. sqlite_master Record Decoding

Hex dump near cell:

```text
00000f8e:
70 01 07 17 1d 1d 01 81 33 ...
```

SQLite table leaf cell format:

```text
[payload size]
[rowid]
[record header]
[column data]
```

---

## Decode

### Payload Size

```text
70 = 112 bytes
```

### Row ID

```text
01 = rowid 1
```

### Serial Types

```text
17 1d 1d 01 81 33
```

These correspond to:

| Column   | Value            |
| -------- | ---------------- |
| type     | table            |
| name     | students         |
| tbl_name | students         |
| rootpage | 2                |
| sql      | CREATE TABLE ... |

This proves SQLite stores schema information internally inside B-tree pages.

---

# 10. students Table Root Page

The `sqlite_master` table revealed:

```text
rootpage = 2
```

Therefore SQLite navigates to page 2 to access actual table rows.

---

# 11. Page 2 Analysis

## Dump of Page 2 Header

Command:

```bash
xxd -s 4096 -l 256 lab.db
```

Output:

```text
00001000: 0d00 0000 050f c600 0ff4 0fea 0fdc 0fd0
00001010: 0fc6
```

---

# 12. Decode Page 2 Header

| Field              | Value                      |
| ------------------ | -------------------------- |
| Page Type          | `0d`                       |
| Number of Cells    | `0005`                     |
| Cell Content Start | `0fc6`                     |
| Cell Pointers      | `0ff4 0fea 0fdc 0fd0 0fc6` |

Meaning:

* Page is a leaf table B-tree page
* Contains 5 records
* One record for each student

---

# 13. Cell Pointer Array

| Pointer | Decimal Offset |
| ------- | -------------- |
| `0ff4`  | 4084           |
| `0fea`  | 4074           |
| `0fdc`  | 4060           |
| `0fd0`  | 4048           |
| `0fc6`  | 4038           |

These pointers reference actual row records.

---

# 14. SQLite Navigation Flow

SQLite internally navigates like this:

```text
SQLite opens page 1
        ↓
Reads sqlite_master
        ↓
Finds root page for students table
        ↓
rootpage = 2
        ↓
Navigates to page 2
        ↓
Reads B-tree leaf cells
        ↓
Retrieves student rows
```

---

# 15. Student Row Decoding

## Raw Hex Dump

```text
00001fc6: 0805 0400 1301 4576 6518
00001fd0: 0a04 0400 1701 4461 7669 6417
00001fdc: 0c03 0400 1b01 4368 6172 6c69 6514
00001fea: 0802 0400 1301 426f 6216
00001ff4: 0a01 0400 1701 416c 6963 6515
```

---

# 16. Alice Row Decode

Bytes:

```text
0a 01 04 00 17 01 41 6c 69 63 65 15
```

Decoded:

| Field        | Value      |
| ------------ | ---------- |
| Payload Size | 10         |
| Row ID       | 1          |
| Header Size  | 4          |
| Serial Types | `00 17 01` |
| Name         | Alice      |
| Age          | 21         |

---

# 17. INTEGER PRIMARY KEY Optimization

Important observation:

Serial type for `id` column:

```text
00
```

Meaning:

* SQLite does NOT store `id` separately
* `INTEGER PRIMARY KEY` maps directly to internal `rowid`

This is an internal SQLite optimization.

---

# 18. Complete Row Mapping

| Row ID | Name    | Age |
| ------ | ------- | --- |
| 1      | Alice   | 21  |
| 2      | Bob     | 22  |
| 3      | Charlie | 20  |
| 4      | David   | 23  |
| 5      | Eve     | 24  |

---

# 19. PRAGMA Verification

Commands:

```sql
PRAGMA page_size;
PRAGMA page_count;
PRAGMA table_info(students);
```

Outputs:

```text
4096
2

0|id|INTEGER|0||1
1|name|TEXT|0||0
2|age|INTEGER|0||0
```

---

# 20. Important Concepts Learned

## SQLite Uses B-Trees

* Tables are stored as B-trees
* Each page stores cells
* Cell pointers reference row locations

---

## Slotted Page Architecture

SQLite stores:

* pointers at top
* records at bottom

This:

* reduces fragmentation
* allows efficient inserts

---

## sqlite_master is Critical

SQLite uses `sqlite_master` to:

* map table names
* locate root pages
* store schema definitions

---

## INTEGER PRIMARY KEY Optimization

SQLite internally maps:

* `INTEGER PRIMARY KEY`
  to:
* `rowid`

This avoids duplicate storage.

---

# 21. Conclusion

In this lab:

* A real SQLite3 database was created
* The database file was inspected using `xxd`
* SQLite headers were decoded manually
* B-tree page structures were analyzed
* sqlite_master navigation was traced
* Table root pages were identified
* Cell pointer arrays were decoded
* Actual student rows were reconstructed directly from raw hexadecimal bytes

This experiment demonstrated how SQLite internally organizes:

* pages
* B-tree nodes
* records
* pointers
* schema metadata

The lab provided practical understanding of low-level database storage engine internals and B-tree based record navigation.
