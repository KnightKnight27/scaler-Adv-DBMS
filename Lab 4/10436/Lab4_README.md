# Lab 4 : SQLite Page Storage Analysis

## Student Details
- **Name:** Romit Raj Sahu
- **Roll Number:** 24BCS10436

---

## Objective

Examine how SQLite physically stores a table inside its database file. The goal is to move beyond SQL and understand what is actually written to disk : which pages exist, how they are structured, and what the raw bytes mean.

---

## Setup

### Database Schema

```sql
PRAGMA page_size = 1024;

CREATE TABLE users (
    id      INTEGER PRIMARY KEY,
    name    TEXT NOT NULL,
    email   TEXT NOT NULL,
    bio     TEXT NOT NULL,
    city    TEXT NOT NULL,
    country TEXT NOT NULL
);
```

The `bio` column is padded to roughly 900 bytes per row so that each row is approximately 1000 bytes total. With a page size of 1024 bytes, each row barely fits on one page. This forces SQLite to create many leaf pages plus one interior (non-leaf) page to hold all the row pointers together. This is exactly what makes it interesting to study.

### Recreating the Database

```bash
sqlite3 my_database.db < setup.sql
```

---

## What is a B-tree and why does SQLite use it?

A **B-tree** is a self-balancing tree data structure used to store sorted data. Each node in the tree holds multiple keys (in our case, row IDs), and each node maps to exactly one page on disk.

SQLite stores every table as a B-tree. There are two types of nodes (pages) in this tree:

- **Leaf pages** : these hold the actual row data (the `INSERT`ed records).
- **Interior pages** : these do NOT hold row data. They only hold pointers to child pages, so SQLite can navigate the tree quickly.

When a table has very few rows, everything fits in a single leaf page and no interior page is needed. Once the table grows large enough that one page cannot hold all the rows, SQLite creates an interior page at the root to point to multiple leaf pages.

---

## Database Metadata

```sql
PRAGMA page_size;
PRAGMA page_count;
SELECT name, type, rootpage FROM sqlite_master ORDER BY rootpage;
```

```
page_size  = 1024 bytes
page_count = 22
file_size  = 22528 bytes  (= 1024 × 22)
```

| Object | Type | Root Page |
|--------|------|-----------|
| `users` | table | 2 |

SQLite also uses **Page 1** internally to store the schema (the list of all tables and indexes). Page 1 is always the `sqlite_schema` page : it is SQLite's own internal catalog.

---

## Page Layout (dbstat)

The `dbstat` virtual table lets us inspect each page without reading raw bytes:

```sql
SELECT pageno, name, pagetype, ncell, payload, unused
FROM dbstat
ORDER BY pageno;
```

| Page | Object | Type | Cells | Notes |
|------|--------|------|-------|-------|
| 1 | `sqlite_schema` | leaf | 1 | Schema entry for the `users` table |
| 2 | `users` | **interior** | 19 | Root of the B-tree; holds child pointers only |
| 3–22 | `users` | leaf | 1 each | One row per page (each row ~950 bytes) |

**Key observation:** Page 2 is an interior page. It does not store any row data : it only stores 19 pointers (child page numbers) and one rightmost child pointer, for a total of 20 leaf pages (pages 3 through 22).

---

## File Header (offset 0x00 – 0x63)

The first 100 bytes of every SQLite file are a fixed header. This header never changes across databases.

```
Offset 0x00:  53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
              "SQLite format 3\0"
```

This 16-byte magic string is how any program can identify a file as a valid SQLite database.

```
Offset 0x10:  04 00
              Page size = 0x0400 = 1024 bytes
```

```
Offset 0x1C:  00 00 00 16
              Page count = 22
```

---

## Page 1: sqlite_schema (offset 0x000, B-tree header at 0x064)

Page 1 is special. The first 100 bytes are the file header above. The B-tree header for this page starts at byte 100 (offset `0x064`).

```
0x064: 0D          → page type 0x0D = leaf table page
0x065–0x066: 00 01 → first freeblock offset
0x067–0x068: 00 01 → cell count = 1
```

There is exactly one cell here, which is the schema record for the `users` table (the `CREATE TABLE` statement stored internally).

**Cell pointer array** (starts at offset `0x064 + 8 = 0x06C`):
```
cell_ptr[0] = 797  → the single schema cell starts 797 bytes into this page
```

---

## Page 2: users Interior Node (offset 0x400)

This is the root of the `users` B-tree. Because our rows are large (~950 bytes each), each leaf page holds only one row. To index 20 leaf pages, SQLite creates this one interior page.

B-tree header starts at offset `0x400`:

```
0x400: 05          → page type 0x05 = interior table page
0x401–0x402: 00 00 → first freeblock offset
0x403–0x404: 00 13 → cell count = 19 (decimal)
0x408–0x40B: 00 00 00 16 → rightmost child = page 22
```

An interior table page has an extra 4-byte field (at header offset +8) that holds the **rightmost child pointer**. This is the child that contains all rows with IDs greater than the largest key in this page.

**Cell pointer array** (starts at offset `0x400 + 12 = 0x40C`):

```
cell_ptr[0]  = 1019
cell_ptr[1]  = 1014
cell_ptr[2]  = 1009
...
cell_ptr[18] = 929
```

Each of these offsets points to a 5-byte interior cell within page 2. Each interior cell contains:
- A 4-byte **left child page number** (which leaf page to follow)
- A **varint** encoding the **rowid divider** (the highest row ID stored in that left child)

This is how SQLite navigates the tree: given a target row ID, it scans the interior page's cells to find which child page to descend into.

---

## Page 3: First Leaf Node (offset 0x800)

This page holds the first row (`id = 1`, `name = 'User_01'`).

B-tree header at offset `0x800`:

```
0x800: 0D          → page type 0x0D = leaf table page
0x803–0x804: 00 01 → cell count = 1
```

**Cell pointer array** (starts at `0x800 + 8 = 0x808`):
```
cell_ptr[0] = 74   → the row data starts 74 bytes into this page
```

**Cell content** at offset `0x800 + 74 = 0x84A`:

```
Hex:   87 33 01 08 00 1b 31 8e 1d 17 17 55 73 65 72 5f 30 31 ...
ASCII: .  3  .  .  .  .  1  .  .  .  .  U  s  e  r  _  0  1  ...
```

Breaking this down:
- The first two bytes are **varints** encoding the payload length and the row ID.
- Then comes a **record header** that describes the data types of each column (as a sequence of type codes).
- Then comes the actual column data in order: `name`, `email`, `bio`, `city`, `country`.

The text `User_01` and `user01@example.com` are visible directly in the hex dump, confirming this is row 1's data.

---

## Interior Page Cell Format

Each cell in an interior page is exactly this:

```
[ 4 bytes: left child page number ] [ varint: divider key (rowid) ]
```

For example, cell at offset 1019 within page 2 (absolute offset `0x400 + 1019 = 0x7FB`):

```
Hex:  00 00 00 03 01
      └── left child = page 3
                  └── rowid divider = 1
```

This means: "All rows with ID ≤ 1 are in page 3."

---

## Row Lookup Walkthrough

To find row with `id = 7`:

1. Start at **Page 2** (interior root).
2. Scan cell pointer array. Find the cell where divider rowid ≥ 7.
3. That cell's left child pointer points to a leaf page (page 9 in this case).
4. Go to **Page 9** (leaf). Read the single row stored there : it has `id = 7`.

This traversal is O(log N) in a general B-tree. Here it is only 2 levels deep because our dataset is small.

---

## Conclusions

- SQLite stores tables as B-trees on disk. Each B-tree node = one page.
- With a page size of 1024 bytes and rows of ~950 bytes, each leaf page holds exactly one row. This forced SQLite to create an interior root page (page 2) to index all 20 leaf pages.
- The first 100 bytes of the file are a fixed header containing the magic string, page size, and page count.
- Interior pages (type `0x05`) store only child pointers and rowid dividers : no row data.
- Leaf pages (type `0x0D`) store the actual row payloads.
- Row data is visible directly in the hex dump : column values appear as plain text within the cell payload.
- `dbstat` and `sqlite_master` let us inspect the logical and physical layout without reading raw bytes manually.
