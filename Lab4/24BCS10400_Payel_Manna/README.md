# Lab 4 - SQLite3 Internal Page and B-Tree Analysis

## Objective

The objective of this lab is to inspect the internal storage structure
of SQLite3 databases using raw hex dumps generated using `xxd`.

This lab demonstrates:
- SQLite database header analysis
- Fixed-size page layout
- B-Tree page organization
- Cell pointer arrays
- Leaf and interior pages
- Record storage format
- Root page traversal
- Real byte-level offset calculations
- Query lookup using B-Tree navigation

---

# Database Schema

## users Table

```sql
CREATE TABLE users (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER
);
```

## products Table

```sql
CREATE TABLE products (
    id INTEGER PRIMARY KEY,
    title TEXT,
    price INTEGER
);
```

---

# Database Statistics

The database was populated using recursive SQL inserts.

Actual database statistics:

| Property | Value |
|---|---|
| Users Rows | 300 |
| Products Rows | 200 |
| Page Size | 4096 bytes |
| Total Pages | 7 |

Commands used:

```sql
SELECT COUNT(*) FROM users;
SELECT COUNT(*) FROM products;
PRAGMA page_size;
PRAGMA page_count;
```

---

# SQLite Database Header Analysis

The first 100 bytes of page 1 contain the SQLite database header.

Real hex dump:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300
```

ASCII representation:

```text
SQLite format 3
```

Important SQLite header fields:

| Offset | Meaning |
|---|---|
| 0-15 | SQLite database signature |
| 16-17 | Database page size |
| 18 | Write version |
| 19 | Read version |

The database signature confirms that the file is a valid SQLite3 database.

---

# SQLite Page Structure

SQLite stores data using fixed-size pages.

Each page contains:

1. Page Header
2. Cell Pointer Array
3. Free Space Region
4. Cell Content Area

General page structure:

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

---

# SQLite B-Tree Structure

SQLite stores tables internally using B+Trees.

The root pages were identified using:

```sql
SELECT name, rootpage FROM sqlite_master;
```

Output:

| Table | Root Page |
|---|---|
| users | 2 |
| products | 3 |

---

# Page Offset Calculations

SQLite stores pages sequentially inside the database file.

Offset calculation formula:

```text
offset = (page_number - 1) * page_size
```

Using page size 4096 bytes:

| Page | File Offset |
|---|---|
| Page 1 | 0 |
| Page 2 | 4096 |
| Page 3 | 8192 |
| Page 4 | 12288 |

Therefore:

```text
users root page offset
= (2 - 1) * 4096
= 4096 bytes
```

```text
products root page offset
= (3 - 1) * 4096
= 8192 bytes
```

These offsets match the generated xxd page dumps.

---

# B-Tree Node Organization

SQLite B-Tree structure:

```text
                 [Root Page]
                /     |     \
           Page2   Page3   Page4
```

Interior pages contain:
- child page pointers
- separator keys

Leaf pages contain:
- actual records
- payload data
- row information

SQLite page types:

| Hex Value | Meaning |
|---|---|
| 0x05 | Interior Table B-Tree Page |
| 0x0D | Leaf Table B-Tree Page |

---

# Cell Pointer Array Analysis

Each SQLite B-Tree page contains a cell pointer array.

Example pointer values:

```text
0fa0
0f80
0f60
```

Interpretation:

| Pointer | Meaning |
|---|---|
| 0x0fa0 | Record starts at offset 0x0fa0 |
| 0x0f80 | Record starts at offset 0x0f80 |
| 0x0f60 | Record starts at offset 0x0f60 |

Cell pointers allow SQLite to:
- efficiently locate records
- maintain sorted storage
- reorganize pages efficiently

Cell pointers grow downward from the top of the page.

Cell content grows upward from the bottom of the page.

---

# SQLite Record Format Analysis

SQLite records are stored in compact binary format.

General record structure:

```text
+----------------------+
| Payload Size         |
+----------------------+
| Row ID               |
+----------------------+
| Record Header        |
+----------------------+
| Column Data          |
+----------------------+
```

Example record bytes:

```text
15 01 04 00 17 01 61 6c 69 63 65
```

Interpretation:

| Bytes | Meaning |
|---|---|
| 15 | Payload size |
| 01 | Row ID |
| 04 | Header size |
| 17 | TEXT serial type |
| 01 | INTEGER serial type |
| 61 6c 69 63 65 | UTF-8 string "alice" |

SQLite uses serial type codes to identify column types.

---

# Query Lookup Traversal

Query analyzed:

```sql
SELECT * FROM users WHERE id = 250;
```

Lookup process:

1. SQLite starts at the root page.
2. SQLite compares the search key.
3. SQLite follows the appropriate child pointer.
4. SQLite reaches the leaf page.
5. SQLite scans the cell pointer array.
6. SQLite locates the matching row.
7. SQLite decodes the record.

Traversal complexity:

```text
O(log n)
```

Advantages:
- efficient disk access
- reduced page reads
- balanced traversal
- scalable storage

---

# Hex Dump Analysis

Hex dumps were generated using:

```bash
xxd db/sample.db
```

Generated dump files:

| File | Purpose |
|---|---|
| full_hex_dump.txt | Complete database dump |
| page1_hex.txt | Database header page |
| page2_hex.txt | users root page |
| page3_hex.txt | products root page |
| interpreted_offsets.txt | Offset calculations |

---

# Real Offset Verification

The root page for the users table is page 2.

Using page size 4096:

```text
offset = (2 - 1) * 4096
       = 4096
```

Therefore the users B-Tree root node begins at byte offset 4096
inside the database file.

Similarly:

```text
products root page offset
= (3 - 1) * 4096
= 8192
```

These offsets match the generated xxd dumps:

```bash
xxd -s 4096 -l 4096 db/sample.db
xxd -s 8192 -l 4096 db/sample.db
```

This verifies the actual physical location of B-Tree pages
inside the SQLite database file.

---

# Files Included

Project structure:

```text
analysis/
db/
dumps/
screenshots/
tools/
README.md
```

Important files:

| File | Description |
|---|---|
| schema.sql | Database schema |
| insert_data.sql | Data generation |
| sample.db | SQLite database |
| full_hex_dump.txt | Full hex dump |
| page_layout.md | Page structure analysis |
| btree_analysis.md | B-Tree analysis |
| lookup_walkthrough.md | Query traversal |
| commands_used.txt | Commands executed |

---

# Conclusion

This lab demonstrated how SQLite internally stores relational data
using page-oriented B-Tree structures.

Using `xxd`, the database file was inspected at byte level to identify:
- SQLite headers
- page organization
- B-Tree nodes
- cell pointer arrays
- record layouts
- child page pointers
- root page offsets

The analysis demonstrates how SQL queries are resolved internally
through low-level B-Tree traversal and page navigation mechanisms.

This lab provided practical understanding of:
- database storage engines
- page-oriented storage
- binary record formats
- B-Tree indexing
- physical data organization in SQLite3