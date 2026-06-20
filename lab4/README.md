# SQLite3 Hex Dump and B-Tree Analysis

## Objective

The objective of this lab is to study the internal storage structure of a SQLite3 database file using a hexadecimal dump. The assignment focuses on understanding:

* SQLite database file structure
* SQLite page layout
* B-tree organization
* Cell pointer arrays
* Record storage
* Row lookup mechanism
* Addresses and offsets inside the database file

A real SQLite database was created and analyzed using a hexadecimal dump generated from the database file.

---

# Database Creation

A SQLite database named `students.db` was created using SQLite3.

The following SQL commands were executed:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    marks INTEGER
);

INSERT INTO students (name, marks)
VALUES
('Archit', 95),
('Rahul', 88),
('Sneha', 91),
('Aman', 76),
('Priya', 89);
```

The table contains 5 rows.

---

# Generating Hex Dump

The hexadecimal dump of the SQLite database file was generated using:

```powershell
Format-Hex students.db > dump.txt
```

The generated dump contains the raw binary structure of the SQLite database.

---

# SQLite File Header Analysis

The first 100 bytes of every SQLite database file contain the database header.

The dump begins with:

```text
00000000   53 51 4C 69 74 65 20 66 6F 72 6D 61 74 20 33 00
```

ASCII Interpretation:

```text
SQLite format 3
```

This identifies the file as a valid SQLite3 database.

---

## Page Size Analysis

The following bytes define the SQLite page size:

```text
00000010   10 00
```

Hexadecimal:

```text
0x1000
```

Decimal conversion:

```text
4096 bytes
```

Therefore:

* SQLite page size = 4096 bytes

This means the database is internally divided into fixed-size pages of 4096 bytes each.

---

# SQLite Page Structure

SQLite stores information inside fixed-size pages.

A typical SQLite page structure is:

```text
+----------------------+
| Page Header          |
+----------------------+
| Cell Pointer Array   |
+----------------------+
| Free Space           |
+----------------------+
| Cell Content Area    |
+----------------------+
```

Explanation:

* Page Header contains metadata about the page.
* Cell Pointer Array stores offsets to records.
* Free Space is unused memory.
* Cell Content Area stores actual row records.

---

# B-Tree Structure in SQLite

SQLite stores tables and indexes using B-tree data structures.

A B-tree allows efficient:

* Searching
* Insertion
* Deletion
* Sorting

The database pages are connected using parent-child relationships.

SQLite uses different page types:

| Hex Value | Page Type                  |
| --------- | -------------------------- |
| 0D        | Table Leaf B-tree Page     |
| 05        | Table Interior B-tree Page |

Since the database is very small, only leaf pages are present.

---

# Analysis of Leaf Table B-Tree Page

The following bytes were observed:

```text
00001000   0D 00 00 00 05 0F C4 00
```

Interpretation:

| Bytes | Meaning                    |
| ----- | -------------------------- |
| 0D    | Leaf Table B-tree Page     |
| 00 00 | First freeblock offset     |
| 00 05 | Number of cells = 5        |
| 0F C4 | Start of cell content area |
| 00    | Fragmented free bytes      |

This confirms that:

* The page is a table leaf page.
* The page contains 5 cells.
* Each cell corresponds to one row.

Since 5 rows were inserted into the table, SQLite created 5 cells inside the B-tree leaf page.

---

# Cell Pointer Array

Immediately after the page header, the following values were observed:

```text
0F F3 0F E7 0F DB 0F D0 0F C4
```

These values represent cell pointers.

Each pointer stores the offset address of a row inside the page.

Interpretation:

| Cell Pointer | Row Location |
| ------------ | ------------ |
| 0F F3        | Row 1        |
| 0F E7        | Row 2        |
| 0F DB        | Row 3        |
| 0F D0        | Row 4        |
| 0F C4        | Row 5        |

SQLite stores rows from the end of the page backwards.

The cell pointer array allows SQLite to quickly locate rows without scanning the entire page.

---

# Record Storage Analysis

The actual row records were found near the end of the page:

```text
00001FC0   00 00 00 00 0A 05 04 00 17 01 50 72 69 79 61 59
00001FD0   09 04 04 00 15 01 41 6D 61 6E 4C 0A 03 04 00 17
00001FE0   01 53 6E 65 68 61 5B 0A 02 04 00 17 01 52 61 68
00001FF0   75 6C 58 0B 01 04 00 19 01 41 72 63 68 69 74 5F
```

ASCII decoding reveals the inserted names:

```text
Priya
Aman
Sneha
Rahul
Archit
```

This confirms that the table records are physically stored inside the B-tree leaf page.

---

# Row Data Decoding

The following bytes represent the row containing "Archit":

```text
0B 01 04 00 19 01 41 72 63 68 69 74 5F
```

Interpretation:

| Byte(s)           | Meaning               |
| ----------------- | --------------------- |
| 0B                | Payload size          |
| 01                | Row ID                |
| 04                | Record header size    |
| 00                | Integer/NULL encoding |
| 19                | TEXT field type       |
| 01                | Integer field type    |
| 41 72 63 68 69 74 | ASCII text = "Archit" |
| 5F                | Marks = 95            |

Hexadecimal value:

```text
5F
```

Decimal conversion:

```text
95
```

This exactly matches the inserted record:

```sql
('Archit', 95)
```

---

# Example of Another Row

The following bytes contain the row for "Sneha":

```text
01 53 6E 65 68 61 5B
```

ASCII:

```text
Sneha
```

Marks value:

```text
5B
```

Hexadecimal:

```text
0x5B
```

Decimal:

```text
91
```

This matches:

```sql
('Sneha', 91)
```

---

# B-Tree Lookup Process

SQLite performs row lookup using the B-tree structure.

The lookup process is:

```text
Root Page
   ↓
Compare Keys
   ↓
Follow Child Pointer
   ↓
Reach Leaf Page
   ↓
Locate Cell Pointer
   ↓
Read Record
```

Since this database contains only 5 rows:

* The root page itself acts as the leaf page.
* No interior pages are required.

For very large databases:

* SQLite creates additional interior B-tree pages.
* The tree height increases.
* Search operations become logarithmic O(log n).

This allows SQLite to efficiently handle large datasets.

---

# Important Observations

1. SQLite stores all table data inside B-tree pages.

2. Every row is stored as a cell.

3. Cell pointers provide direct access to rows.

4. SQLite stores rows from the end of the page backwards.

5. The database file contains both metadata and actual table records.

6. ASCII text values can be directly observed inside the hex dump.

7. SQLite internally uses fixed-size pages for storage management.

8. B-tree structures allow efficient searching and indexing.

---

# Conclusion

This lab demonstrated the internal working of SQLite3 database storage using a real hexadecimal dump.

The analysis showed:

* SQLite database headers
* Page organization
* B-tree leaf pages
* Cell pointer arrays
* Row record encoding
* Physical storage of records
* Address-based navigation
* B-tree lookup mechanisms

The hexadecimal dump clearly demonstrated how SQLite organizes data internally using B-tree structures and fixed-size pages.

This experiment provided practical understanding of low-level database storage mechanisms used by SQLite3.
