# Lab 4 — SQLite On-Disk Format (Byte-Level Walkthrough)

**Name:** Akshat Kushwaha
**Roll No:** 24BCS10060

## What this lab is about

In Lab 2 I learned SQLite keeps the whole database in one file made of fixed
pages. In this lab I actually opened that file with a hex viewer and read the
bytes to see *how* a table, its rows, and an index are laid out. I used a small
`books` table with a `UNIQUE` column so SQLite would also build an index, and
then walked through the resulting file byte by byte.

## Files

| File | What it is |
|---|---|
| `books.sql` | the schema + 5 inserts + `VACUUM` |
| `books.db` | the 12,288-byte SQLite database (3 pages) |
| `books.hex` | full `xxd -g 1 -c 16` hex dump of the file |
| `README.md` | this walkthrough |

## How to reproduce

```bash
rm -f books.db
sqlite3 books.db < books.sql
xxd -g 1 -c 16 books.db > books.hex
```

After `VACUUM` the file is exactly **3 pages × 4096 = 12,288 bytes**:

```
0x0000  page 1  -> schema (sqlite_master)              leaf table  (0x0d)
0x1000  page 2  -> the actual book rows                leaf table  (0x0d)
0x2000  page 3  -> the UNIQUE isbn index               leaf index  (0x0a)
```

---

## Page 1 — the 100-byte file header (offset 0x0000)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 03 00 00 00 03  .....@  ........
```

| Offset | Bytes | Meaning |
|---|---|---|
| 0x00 | `53 51 4c ... 33 00` | the magic string `"SQLite format 3\0"` |
| 0x10 | `10 00` | page size = `0x1000` = **4096** |
| 0x12 | `01 01` | file format write=1, read=1 |
| 0x1C | `00 00 00 03` | database size = **3 pages** |

The fixed file header is 100 bytes, so the first B-tree header starts right
after it at **0x0064**.

## Page 1 — the schema (sqlite_master)

```
00000060: 00 2e 8d f8 0d 0f ec 00 02 0f 12 00 0f 12 0f c1  ................
```

The first 4 bytes here (`00 2e 8d f8`) are still the tail of the file header
(the SQLite version number). The page-1 B-tree header begins at **0x0064**:

| Offset | Bytes | Meaning |
|---|---|---|
| 0x64 | `0d` | page type = leaf table |
| 0x67 | `00 02` | **2 cells** on this page |
| 0x69 | `0f 12` | cell content starts at 0x0f12 |

The 2 cell pointers at **0x006C** are `0f 12` and `0f c1`, so the two schema
rows sit at file offsets **0x0f12** and **0x0fc1**.

### The `books` table row @ 0x0f12

```
00000f10: 00 00 81 2c 01 07 17 17 17 01 82 37 74 61 62 6c  ...,.......7tabl
00000f20: 65 62 6f 6f 6b 73 62 6f 6f 6b 73 02 43 52 45 41  ebooksbooks.CREA
00000f30: 54 45 20 54 41 42 4c 45 20 62 6f 6f 6b 73 20 28  TE TABLE books (
```

Decoding the record:
- `81 2c` → payload size (a varint) = 172 bytes
- `01` → rowid 1
- `07` → record header length 7
- `17 17 17 01 82 37` → the column serial types:
  - `0x17` text(5) → `"table"`
  - `0x17` text(5) → `"books"` (the table name)
  - `0x17` text(5) → `"books"` (tbl_name)
  - `0x01` int1 → **rootpage = 2** ← so the book rows live on page 2
  - `0x82 37` text → the full `CREATE TABLE books (...)` SQL

You can literally read `tablebooksbooks` then the `CREATE TABLE` text in the
ASCII column on the right.

### The auto-index row @ 0x0fc1

```
00000fc0: 29 29 02 06 17 3d 17 01 00 69 6e 64 65 78 73 71  ))...=...indexsq
00000fd0: 6c 69 74 65 5f 61 75 74 6f 69 6e 64 65 78 5f 62  lite_autoindex_b
00000fe0: 6f 6f 6b 73 5f 31 62 6f 6f 6b 73 03              ooks_1books.
```

- `29` → payload size 41
- `02` → rowid 2
- `06 17 3d 17 01 00` → header: text(5)=`"index"`, text(24)=`"sqlite_autoindex_books_1"`,
  text(5)=`"books"`, int1=**3** (rootpage), NULL (an auto-index has no SQL)
- So this index lives on **page 3**.

The `UNIQUE` constraint on `isbn` is why SQLite created this index automatically.

---

## Page 2 — the book rows (offset 0x1000)

```
00001000: 0d 00 00 00 05 0f 09 00 0f c4 0f 8e 0f 63 0f 34  .............c.4
00001010: 0f 09 ...
```

| Offset | Bytes | Meaning |
|---|---|---|
| 0x1000 | `0d` | leaf table page |
| 0x1003 | `00 05` | **5 cells** = 5 rows |
| 0x1005 | `0f 09` | content starts at 0x0f09 in the page (= 0x1f09 in the file) |

The 5 cell pointers at **0x1008** are `0f c4 0f 8e 0f 63 0f 34 0f 09`, so the
rows are at file offsets 0x1fc4, 0x1f8e, 0x1f63, 0x1f34, 0x1f09.

### Row for rowid 1 @ 0x1fc4

```
00001fc0: 31 30 30 32 2e 01 06 00 31 23 02 1f 44 61 74 61  1002....1#..Data
00001fd0: 62 61 73 65 20 49 6e 74 65 72 6e 61 6c 73 41 6c  base InternalsAl
00001fe0: 65 78 20 50 65 74 72 6f 76 07 e3 49 53 42 4e 2d  ex Petrov..ISBN-
00001ff0: 31 30 30 31 ...                                   1001
```

Decoding:
- `2e` → payload size 46
- `01` → rowid 1
- `06` → header length 6
- serial types `00 31 23 02 1f`:
  - `00` NULL → the `id` column (it's an alias of rowid, so it's stored as NULL)
  - `0x31` text(18) → `"Database Internals"`
  - `0x23` text(11) → `"Alex Petrov"`
  - `0x02` 2-byte int → year
  - `0x1f` text(9) → `"ISBN-1001"`
- body bytes: `Database Internals` `Alex Petrov` `07 e3` `ISBN-1001`

The year is the two bytes `07 e3` = `0x07E3` = **2019**, which is exactly the
year I inserted for that book.

How text lengths are read: SQLite stores a text column's serial type as
`(length × 2) + 13`. So `0x31 = 49 → (49−13)/2 = 18` characters, `0x23 = 35 →
11` characters, `0x1f = 31 → 9` characters. There are no separators between the
values — the header lengths are what tell the reader where each one ends.

---

## Page 3 — the UNIQUE isbn index (offset 0x2000)

```
00002000: 0a 00 00 00 05 0f af 00 0f e7 0f d9 0f cb 0f bd  ................
00002010: 0f af ...
```

| Offset | Bytes | Meaning |
|---|---|---|
| 0x2000 | `0a` | **leaf index** page (not a table) |
| 0x2003 | `00 05` | 5 cells = 5 index entries |

The cell pointers at **0x2008** are `0f e7 0f d9 0f cb 0f bd 0f af`, and they are
in **key order** (smallest isbn first):

```
00002fa0: ... 0d 03 1f 01 49 53 42 4e 2d 31 30 30 35 05    ....ISBN-1005.
00002fe0: 4e 2d 31 30 30 32 02 0c 03 1f 09 49 53 42 4e 2d  N-1002.....ISBN-
00002ff0: 31 30 30 31 ...                                  1001
```

Reading the smallest-key entry (at 0x2fe7):
- `0c` → payload 12
- `03` → header length 3
- `1f` text(9) → the isbn key `"ISBN-1001"`
- `09` → serial type 9, which is the constant integer **1** (the rowid)

So this entry says **isbn "ISBN-1001" → rowid 1**. Listing all 5 (the index is
sorted by isbn):

| isbn key | rowid |
|---|---|
| ISBN-1001 | 1 |
| ISBN-1002 | 2 |
| ISBN-1003 | 3 |
| ISBN-1004 | 4 |
| ISBN-1005 | 5 |

---

## How a lookup uses all this

**`SELECT * FROM books WHERE id = 3`** (lookup by rowid):
1. Read page 1, find `books` → rootpage 2.
2. Read page 2, find the cell with rowid 3.
3. Decode the row. → **2 page reads.**

**`SELECT * FROM books WHERE isbn = 'ISBN-1004'`** (lookup by indexed column):
1. Read page 1, find `sqlite_autoindex_books_1` → rootpage 3.
2. Read page 3 (the index), binary-search the sorted isbn keys → finds rowid 4.
3. Read page 2, find the row with rowid 4.
4. Decode it. → **3 page reads** — one extra hop, but no full table scan.

With only 5 rows every page is a single leaf. With thousands of rows SQLite
would add interior pages (`0x05` for tables, `0x02` for indexes) holding child
pointers, turning each B-tree into a multi-level tree.

## Key takeaways

- A SQLite database is one file split into fixed 4 KB pages.
- Page 1 starts with a 100-byte header and then holds `sqlite_master`, the schema
  that points to where every table and index lives (its rootpage).
- Each page begins with a 1-byte type: `0x0d` leaf table, `0x0a` leaf index.
- A row is stored as `[payload size][rowid][header of serial types][values]`,
  with no separators — the serial types encode each value's type and length.
- The `id INTEGER PRIMARY KEY` column is stored as NULL because it's just an
  alias for the rowid.
- A `UNIQUE` column makes SQLite build a separate index page that maps the key to
  the rowid, which is what makes indexed lookups avoid a full scan.
