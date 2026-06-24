# SQLite3 Internal Structure and Hex Dump Analysis

## Student Information

| Field | Details |
|---|---|
| **Name** | Namami Verma |
| **Roll Number** | 24BCS10349 |
| **Lab** | SQLite3 Internal Structure and Hex Dump Analysis |

---

## Objective

The objective of this lab is to study the internal structure of a SQLite3 database file using hexadecimal analysis. The lab demonstrates how SQLite stores records internally using B-Tree pages, cell pointer arrays, and page-based storage.

**Tools used:**
- `sqlite3`
- `xxd`
- `strings`
- Linux terminal

---

## Files in This Repository

| File Name | Description |
|---|---|
| `lab.db` | SQLite database file |
| `dump.txt` | Hex dump of the database |
| `setup.sh` | Shell script to recreate the lab from scratch |
| `README.md` | Internal structure analysis |

---

## Database Creation

### Command Used
```bash
sqlite3 lab.db
```

### Table Creation
```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

### Inserted Records
```sql
INSERT INTO students(name, age) VALUES
('Alice',21),('Bob',22),('Charlie',23),('David',24),
('Eve',25),('Frank',26),('Grace',27),('Helen',28);
```

---

## Verification of Data

### Command
```sql
SELECT * FROM students;
```

### Output
```
1|Alice|21
2|Bob|22
3|Charlie|23
4|David|24
5|Eve|25
6|Frank|26
7|Grace|27
8|Helen|28
```

---

## SQLite Database File Information

| Property | Value |
|---|---|
| File | `lab.db` |
| File Size | 8 KB |

---

## SQLite Page Information

### Page Size

```bash
sqlite3 lab.db "PRAGMA page_size;"
```

**Output:** `4096`

SQLite organizes the database into fixed-size pages. Each page in this database is **4096 bytes**.

### Total Number of Pages

```bash
sqlite3 lab.db "PRAGMA page_count;"
```

**Output:** `2`

| Page Number | Address Range | Purpose |
|---|---|---|
| Page 1 | `0x0000 – 0x0FFF` | SQLite header and schema |
| Page 2 | `0x1000 – 0x1FFF` | students table records |

---

## Generating Hex Dump

```bash
xxd -g 1 lab.db > dump.txt
```

This command generates a hexadecimal dump of the entire SQLite database file.

---

## SQLite Header Analysis

```bash
xxd -l 64 lab.db
```

### Output
```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
```

---

## SQLite Signature Analysis

The first 16 bytes contain the SQLite database signature.

| Hex Bytes | ASCII | Meaning |
|---|---|---|
| `53 51 4c 69` | `SQLi` | SQLite identifier |
| `74 65 20 66` | `te f` | Part of signature |
| `6f 72 6d 61` | `orma` | Part of signature |
| `74 20 33 00` | `t 3.` | SQLite format version |

The complete signature is: `SQLite format 3`

This confirms the file is a valid SQLite3 database.

---

## SQLite Page Size Analysis

At offset `0x10` (byte 16–17):
```
10 00  →  0x1000  →  4096 decimal
```

This confirms the SQLite page size is **4096 bytes**.

---

## B-Tree Structure in SQLite

SQLite stores tables internally using B-Tree structures. B-Trees provide:

- Fast searching
- Efficient insertion
- Efficient deletion
- Ordered storage of records

### Page Type Values

| Page Type | Hex Value | Meaning |
|---|---|---|
| Leaf Table Page | `0x0D` | Stores actual rows |
| Interior Table Page | `0x05` | Stores child pointers |
| Leaf Index Page | `0x0A` | Stores index values |
| Interior Index Page | `0x02` | Stores index navigation |

---

## Real B-Tree Page Analysis (Page 2)

```bash
xxd -s 4096 -l 128 -g 1 lab.db
```

### Output
```
00001000: 0d 00 00 00 08 0f a2 00 0f f4 0f ea 0f dc 0f d0  ................
00001010: 0f c6 0f ba 0f ae 0f a2 00 00 00 00 00 00 00 00  ................
00001020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00001030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

---

## B-Tree Header Analysis

| Offset | Bytes | Value | Meaning |
|---|---|---|---|
| 0 | 1 | `0d` | Leaf table B-tree page |
| 1–2 | 2 | `0000` | No freeblock |
| 3–4 | 2 | `0008` | Number of cells = 8 |
| 5–6 | 2 | `0fa2` | Start of cell content area |
| 7 | 1 | `00` | Fragmented free bytes |

### Page Type: `0x0D`
This indicates a **Leaf Table B-Tree Page** — stores actual table row data.

### Number of Cells: `0x0008` = 8
This matches the 8 inserted student records exactly.

### Cell Content Area: `0x0FA2`
SQLite stores records from the **bottom of the page upward**, beginning at offset `0x0FA2` inside the page.

---

## Cell Pointer Array Analysis

The cell pointer array begins immediately after the 8-byte page header.

### Cell Pointer Values
```
0f f4
0f ea
0f dc
0f d0
0f c6
0f ba
0f ae
0f a2
```

Each pointer is a 2-byte offset pointing to a row record inside the page.

---

## Real Cell Addresses

Page 2 starts at: `0x1000`

**Formula:** `Real Address = Page Start (0x1000) + Cell Offset`

| Cell Pointer | Decimal Offset | Real Address |
|---|---|---|
| `0ff4` | 4084 | `0x1ff4` |
| `0fea` | 4074 | `0x1fea` |
| `0fdc` | 4060 | `0x1fdc` |
| `0fd0` | 4048 | `0x1fd0` |
| `0fc6` | 4038 | `0x1fc6` |
| `0fba` | 4026 | `0x1fba` |
| `0fae` | 4014 | `0x1fae` |
| `0fa2` | 4002 | `0x1fa2` |

These are the actual physical locations of each student record inside the database file.

---

## Record Storage in SQLite

Each SQLite record cell contains:

| Component | Description |
|---|---|
| Payload Size | Total size of the record |
| Row ID | Primary key value |
| Record Header | Metadata (column count, types) |
| Serial Types | Data type codes for each column |
| Payload Data | Actual column values |

---

## Extracting Stored Strings

```bash
strings lab.db
```

### Output
```
SQLite format 3
table
students
students
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
Helen
Grace
Frank
David
Charlie
Alice
```

This confirms SQLite stores the table schema, CREATE statement, and all text payloads directly inside the B-Tree leaf pages.

---

## SQLite Lookup Process

Example query:
```sql
SELECT * FROM students WHERE id = 5;
```

SQLite performs these steps internally:

1. Start at the root B-Tree page
2. Read the page header
3. Read the cell pointer array
4. Compare row IDs in cells
5. Navigate to the matching cell
6. Read the payload data

---

## Important Observations

1. SQLite databases are **page-based** — all data lives inside fixed-size pages.
2. Each page has a **fixed size** (4096 bytes in this lab).
3. Tables are stored as **B-Trees** for efficient access.
4. **Leaf pages** store actual row data.
5. **Cell pointer arrays** reference the physical location of each row.
6. Records are stored **bottom to top** inside a page.
7. SQLite stores text payload **directly** in the leaf page — no external storage.

---

## Conclusion

This lab demonstrated the internal working of SQLite3 database storage through hexadecimal analysis. The experiment confirmed:

- SQLite uses a **page-oriented architecture** (4096 bytes/page)
- Page 1 holds the **database header and schema**
- Page 2 holds the **students table as a B-Tree leaf page**
- The **B-Tree page header** at `0x1000` encodes page type, cell count, and content area start
- **Cell pointer arrays** map each row to its physical byte offset
- All 8 student records are stored at addresses `0x1fa2` through `0x1ff4`
- SQLite's B-Tree structure enables O(log n) lookup complexity

The hex dump and PRAGMA outputs together provide a complete picture of how SQLite physically stores, organizes, and retrieves relational data.