# SQLite3 Internal Structure Analysis

## Student Information

- **Name**: Rohan Ranjan
- **Roll Number**: 24BCS10428
- **Lab**: SQLite3 Internal Structure and Hex Dump Analysis

---

# Objective

The objective of this lab is to study the internal structure of a SQLite3 database file using hexadecimal analysis. The lab demonstrates how SQLite stores records internally using B-Tree pages, cell pointer arrays, and page-based storage.

Tools used:
- `sqlite3`
- `xxd`
- `strings`
- Linux terminal

---

# Database Creation

## Command Used

```bash
sqlite3 lab.db
```

## Table Creation

```sql
CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

## Inserted Records

```sql
INSERT INTO students(name, age) VALUES
('Alice',21),
('Bob',22),
('Charlie',23),
('David',24),
('Eve',25),
('Frank',26),
('Grace',27),
('Helen',28);
```

---

# Verification of Data

## Command Used

```sql
SELECT * FROM students;
```

## Output

```text
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

# SQLite Database File Information

- **File Created**: `lab.db`
- **File Size**: `8 KB (8192 bytes)`

---

# SQLite Page Information

## Page Size

### Command Used

```bash
sqlite3 lab.db "PRAGMA page_size;"
```

### Output

```text
4096
```

### Explanation

SQLite organizes the database into fixed-size pages. Each page in this database is **4096 bytes** long.

---

# Total Number of Pages

## Command Used

```bash
sqlite3 lab.db "PRAGMA page_count;"
```

## Output

```text
2
```

## Explanation

The database contains exactly 2 pages.

| Page Number | Address Range | Purpose |
|---|---|---|
| Page 1 | `0x0000 – 0x0FFF` | SQLite header and database schema (`sqlite_schema` table) |
| Page 2 | `0x1000 – 0x1FFF` | `students` table records (Leaf Table B-Tree) |

---

# Generating Hex Dump

## Command Used

```bash
xxd -g 1 lab.db > dump.txt
```

This command generates a structured hexadecimal dump of the SQLite database file saved as `dump.txt`.

---

# SQLite Header Analysis

## Command Used

```bash
xxd -l 64 lab.db
```

## Output

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 02 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
```

---

# SQLite Signature Analysis

The first 16 bytes contain the SQLite database signature.

| Hex Bytes | ASCII | Meaning |
|---|---|---|
| `53 51 4c 69` | `SQLi` | SQLite identifier |
| `74 65 20 66` | `te f` | Part of signature |
| `6f 72 6d 61` | `orma` | Part of signature |
| `74 20 33 00` | `t 3\0` | SQLite format version 3 |

The complete signature is:
```text
SQLite format 3
```
This signature confirms that the file is a valid SQLite3 database file.

---

# SQLite Page Size Analysis

At offset 16 (first two bytes of line `0x00000010`):
```text
10 00
```
- **Hexadecimal value**: `0x1000`
- **Decimal equivalent**: `4096`

This confirms that the SQLite database page size is dynamically set to **4096 bytes**.

---

# B-Tree Structure in SQLite

SQLite stores tables internally using B-Tree structures. B-Trees provide logarithmic time complexity for searching, insertion, and deletion operations.

SQLite pages are categorized into:

| Page Type | Hex Value | Meaning |
|---|---|---|
| **0x0D** | `Leaf Table Page` | Stores actual table row records |
| **0x05** | `Interior Table Page` | Stores child page pointers |
| **0x0A** | `Leaf Index Page` | Stores index values |
| **0x02** | `Interior Index Page` | Stores index navigation pointers |

---

# Real B-Tree Page Analysis

## Command Used

```bash
xxd -s 4096 -l 128 -g 1 lab.db
```

## Output

```text
00001000: 0d 00 00 00 08 0f 96 00 0f e8 0f de 0f d0 0f c4  ................
00001010: 0f ba 0f ae 0f a2 0f 96 00 00 00 00 00 00 00 00  ................
00001020: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00001030: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

---

# B-Tree Header Analysis

The beginning of the B-Tree page (Page 2, starting at byte offset `4096` / `0x1000`) contains the B-Tree page header.

| Offset in Page | Bytes | Value | Meaning |
|---|---|---|---|
| 0 | 1 | `0d` | Leaf table B-Tree page |
| 1–2 | 2 | `0000` | No freeblocks |
| 3–4 | 2 | `0008` | Number of cells = 8 |
| 5–6 | 2 | `0f96` | Start of cell content area |
| 7 | 1 | `00` | Fragmented free bytes |

---

# Explanation of B-Tree Header

## Page Type

```text
0d
```
This indicates the page is a **Leaf Table B-Tree Page** containing actual row data.

---

## Number of Cells

```text
0008
```
This means the page contains **8 cells (8 rows)**, which matches the 8 student records inserted.

---

## Cell Content Area

```text
0f96
```
This indicates that the cell content area begins at offset **0x0F96** inside the page. SQLite stores records starting from the bottom of the page upward, leaving the middle section free for growth.

---

# Cell Pointer Array Analysis

The cell pointer array begins immediately after the 8-byte B-Tree page header.

## Cell Pointer Values

```text
0f e8
0f de
0f d0
0f c4
0f ba
0f ae
0f a2
0f 96
```
Each pointer references the location offset of a row record inside Page 2.

---

# Cell Pointer Address Calculation

Page 2 starts at physical byte address:
```text
0x1000
```
Formula used:
```text
Real Address = Page Start + Cell Offset
```

---

# Real Cell Addresses

| Cell Pointer | Decimal Offset | Real Address |
|---|---|---|
| `0fe8` | 4072 | `0x1fe8` |
| `0fde` | 4062 | `0x1fde` |
| `0fd0` | 4048 | `0x1fd0` |
| `0fc4` | 4036 | `0x1fc4` |
| `0fba` | 4026 | `0x1fba` |
| `0fae` | 4014 | `0x1fae` |
| `0fa2` | 4002 | `0x1fa2` |
| `0f96` | 3990 | `0x1f96` |

These addresses represent the physical locations of the stored records inside the `lab.db` database file.

---

# Record Storage in SQLite

Each SQLite record is packed as a payload consisting of:
- **Payload Size**: Variable-length integer (varint) representing size of record
- **Row ID**: Primary key value (varint)
- **Record Header**: Variable-length header describing the column types
- **Serial Types**: Codes indicating data type information for columns
- **Payload Data**: The actual data bytes of the row (e.g. `'Alice'`, `21`)

---

# Extracting Stored Strings

## Command Used

```bash
strings lab.db
```

## Output

```text
SQLite format 3
tablestudentsstudents
CREATE TABLE students(id INTEGER PRIMARY KEY, name TEXT, age INTEGER)
Helen
Grace
Frank
David
Charlie
Alice
```

---

# Explanation of String Output

The `strings` utility extracts ASCII text sequences that are 4 or more characters long.

- **Schema Definition**: SQLite stores the table metadata directly in Page 1 (schema table).
- **Table Data**: Stored text payloads like `'Alice'`, `'Charlie'`, `'David'`, `'Frank'`, `'Grace'`, and `'Helen'` are printed directly from the B-Tree leaf page.
- *Note*: `'Bob'` and `'Eve'` are omitted because they have a length of 3 characters, falling below the default 4-character minimum threshold of the `strings` tool.

---

# SQLite Lookup Process

When running the query:
```sql
SELECT * FROM students WHERE id = 5;
```

SQLite traverses the database using the following internal steps:
1. **Locate Root**: Looks up the root B-Tree page for the `students` table from the schema.
2. **Read Header**: Inspects the page header of Page 2 to find cell pointer locations.
3. **Scan Cell Pointers**: Reads the cell pointer array, comparing the row ID (Primary Key) sequentially or via binary search.
4. **Access Physical Location**: Identifies cell pointer offset `0x0fc4` representing real physical address `0x1fc4`.
5. **Parse Payload**: Decodes record values: `5`, `'Eve'`, and `25`.

---

# Important Observations

1. **Page-Based Storage**: SQLite databases are composed of fixed-size, page-aligned blocks.
2. **Internal Structures**: SQLite represents relational tables as B-Trees.
3. **Data Storage direction**: Leaf pages store actual table data at the bottom of the page growing upwards, while headers and cell pointer arrays grow from the top downwards.
4. **Binary Compatibility**: Text fields are stored in standard UTF-8/UTF-16 encoding, making them readable via simple text filters.

---

# Files Generated

| File Name | Description |
|---|---|
| `lab.db` | Binary SQLite database file containing structured schema and records |
| `dump.txt` | Detailed hex dump of `lab.db` generated using `xxd` |
| `README.md` | Comprehensive database structure and analysis report |

---

# Conclusion

This lab demonstrated the internal working of SQLite3 database storage using hexadecimal analysis. The experiment mapped:
- The exact byte-level representation of SQLite pages and headers.
- The layout of a B-Tree leaf page.
- How records are packed and referenced using a cell pointer array.
- Mathematical offset-to-physical address conversions.

The analysis verified that SQLite uses a highly compact and optimized page-oriented B-Tree structure to manage and query relational data.
