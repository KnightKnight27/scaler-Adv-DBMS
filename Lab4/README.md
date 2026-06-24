# Lab 4 — SQLite3 Internal Structure (Hex Dump Analysis)

**24BCS10079 — Piyush Bansal**

The goal of this lab was to actually *see* how SQLite lays a database out on disk. I built a small `library` database, dumped the file with `xxd`, and then walked through the bytes to decode the file header, the schema page, the table B-tree, and the index B-tree.

What surprised me the most: every concept from class — pages, B-trees, cell pointers, varints, serial types — shows up in the dump as plain bytes I can point at. Once I lined the hex up next to the SQLite file format spec, the structure stopped feeling abstract.

---

## 1. Setup

I used a tiny `books` schema with one secondary index on `pages` so I would end up with three distinct pages in the file (schema page, table B-tree, index B-tree).

### Schema (`create_library.sql`)

```sql
CREATE TABLE books (
    id     INTEGER PRIMARY KEY,
    title  TEXT NOT NULL,
    pages  INTEGER,
    genre  TEXT
);

CREATE INDEX idx_books_pages ON books(pages);

INSERT INTO books (title, pages, genre) VALUES
('Refactoring', 448, 'Tech'),
('Clean Code',  464, 'Tech'),
('Sapiens',     498, 'History'),
('Dune',        688, 'SciFi'),
('Hobbit',      310, 'Fantasy'),
('1984',        328, 'Fiction'),
('Atomic Habits', 320, 'SelfHelp'),
('Deep Work',   304, 'SelfHelp'),
('CLRS',       1312, 'Tech'),
('SICP',        657, 'Tech'),
('Foundation',  255, 'SciFi'),
('Brave New World', 311, 'Fiction');

VACUUM;
```

I ran `VACUUM` at the end so the pages are laid out cleanly — without it the file picks up holes from inserts and the offsets get harder to follow.

### Commands

```bash
sqlite3 library.db < create_library.sql
xxd -g 1 library.db > library.hex
```

### Quick file check

```bash
$ ls -l library.db
-rw-r--r--  1 piyushbansal  staff  12288  library.db
```

12288 bytes ÷ 4096 = **3 pages**. That matches what `.dbinfo` reports below.

---

## 2. Whole-file Overview

`.dbinfo` is the easiest way to confirm what I am about to find in the hex.

```
$ sqlite3 library.db ".dbinfo"
database page size:  4096
write format:        1
read format:         1
reserved bytes:      12
file change counter: 4
database page count: 3
freelist page count: 0
schema cookie:       3
schema format:       4
text encoding:       1 (utf8)
number of tables:    1
number of indexes:   1
```

```
$ sqlite3 library.db "SELECT type, name, rootpage FROM sqlite_schema;"
table|books|2
index|idx_books_pages|3
```

So I expect the file to look like this:

| Page | File offset | What lives there                            |
|------|-------------|---------------------------------------------|
| 1    | `0x0000`    | File header (100 B) + `sqlite_schema` B-tree |
| 2    | `0x1000`    | Table B-tree for `books`                    |
| 3    | `0x2000`    | Index B-tree for `idx_books_pages`          |

> One thing I had to look up: on my Mac, SQLite reserves **12 bytes** at the end of every page (the `reserved bytes: 12` line above). That is why the cell content area on each page tops out a little short of 4096.

---

## 3. The Database File Header (bytes `0x00` — `0x63`)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 04 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
```

Decoding the fields I care about, using the [SQLite file format spec](https://www.sqlite.org/fileformat.html):

| Offset | Bytes | Value | Meaning |
|--------|-------|-------|---------|
| `0x00 – 0x0F` | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `"SQLite format 3\0"` | The magic string. Every SQLite database begins with this. |
| `0x10 – 0x11` | `10 00` | `4096` | Page size. `0x1000` is exactly 4 KiB. |
| `0x12` | `01` | `1` | Write format (1 = legacy / rollback journal). |
| `0x13` | `01` | `1` | Read format (1 = legacy). |
| `0x14` | `0c` | `12` | Reserved bytes per page (the macOS-specific 12 I mentioned earlier). |
| `0x15` | `40` | `64` | Max embedded payload fraction (required to be 64). |
| `0x16` | `20` | `32` | Min embedded payload fraction (required to be 32). |
| `0x17` | `20` | `32` | Leaf payload fraction (required to be 32). |
| `0x18 – 0x1B` | `00 00 00 04` | `4` | File change counter — increments on each write. I ran `VACUUM` which counts. |
| `0x1C – 0x1F` | `00 00 00 03` | `3` | Database size in pages — matches my `.dbinfo` output. |
| `0x2C – 0x2F` | `00 00 00 03` | `3` | Schema cookie. |
| `0x38 – 0x3B` | `00 00 00 01` | `1` | Text encoding (1 = UTF-8). |

This was the moment the lab clicked for me: the header isn't a binary blob, it's a tidy struct with each field at a known offset.

---

## 4. B-Tree Page Type Flags (a quick reference)

Every page begins with a one-byte flag that tells SQLite what kind of B-tree page it is:

| Flag | Value | Page type            |
|------|-------|----------------------|
| `0x02` | 2   | Index interior page  |
| `0x05` | 5   | Table interior page  |
| `0x0a` | 10  | Index leaf page      |
| `0x0d` | 13  | Table leaf page      |

For a database this small, every B-tree fits on a single leaf page, so I only see `0x0d` and `0x0a` in this file.

---

## 5. Page 1 — `sqlite_schema`

Page 1 is special: the first 100 bytes are the file header (already decoded above), and the B-tree page header starts at offset `0x64` (100 in decimal).

### B-tree page header (`0x64` — `0x6B`)

```
00000060: 00 2e 8d f8 0d 00 00 00 02 0f 18 00 0f 66 0f 18  .............f..
                      ^^ page header begins at byte 0x64
```

- `0x64` = `0d` → **Table Leaf Page**.
- `0x65 – 0x66` = `00 00` → first freeblock offset = 0 (no freeblocks).
- `0x67 – 0x68` = `00 02` → **2 cells** on this page. One for the table `books`, one for the index `idx_books_pages`.
- `0x69 – 0x6A` = `0f 18` → cell content area starts at page offset `0x0f18` (file offset `0x0f18`).
- `0x6B` = `00` → 0 fragmented free bytes.

### Cell pointer array (`0x6C` — `0x6F`)

Two cells means two 2-byte pointers:

| Pointer index | Bytes | Page offset of cell |
|---------------|-------|---------------------|
| 0 | `0f 66` | `0x0f66` |
| 1 | `0f 18` | `0x0f18` |

So cells live at the bottom of the page (`0x0f18` and `0x0f66`), and the pointer array grows from the top — exactly the "two stacks meeting in the middle" layout we discussed in class.

### Cell 0 — the `books` table entry (`0x0f66`)

```
00000f60: 70 61 67 65 73 29 81 0b 01 07 17 17 17 01 81 75  pages).........u
00000f70: 74 61 62 6c 65 62 6f 6f 6b 73 62 6f 6f 6b 73 02  tablebooksbooks.
00000f80: 43 52 45 41 54 45 20 54 41 42 4c 45 20 62 6f 6f  CREATE TABLE boo
00000f90: 6b 73 20 28 0a 20 20 20 20 69 64 20 20 20 20 20  ks (.    id     
```

Walking through the cell starting at `0x0f66`:

1. **Payload size (varint)**: `81 0b` → `(0x81 & 0x7f) << 7 | 0x0b = 0x80 | 0x0b = 139` bytes.
2. **RowID (varint)**: `01` → 1.
3. **Record header size (varint)**: `07` → 7 bytes.
4. **Serial-type array** (5 columns of `sqlite_schema`: `type, name, tbl_name, rootpage, sql`):
   - `17` → text, `(23 − 13) / 2 = 5` bytes → `"table"`.
   - `17` → text, 5 bytes → `"books"`.
   - `17` → text, 5 bytes → `"books"`.
   - `01` → 8-bit signed int → `rootpage = 2`.
   - `81 75` → varint `(0x81 & 0x7f) << 7 | 0x75 = 245`. Text length = `(245 − 13) / 2 = 116` → the `CREATE TABLE …` SQL.
5. **Values**: `"table"`, `"books"`, `"books"`, `2`, `"CREATE TABLE books ( … )"` — which I can literally read in the ASCII column of the dump.

### Cell 1 — the `idx_books_pages` entry (`0x0f18`)

```
00000f10: 00 00 00 00 00 00 00 00 4c 02 06 17 2b 17 01 65  ........L...+..e
                                ^^ cell starts at 0x0f18
00000f20: 69 6e 64 65 78 69 64 78 5f 62 6f 6f 6b 73 5f 70  indexidx_books_p
00000f30: 61 67 65 73 62 6f 6f 6b 73 03 43 52 45 41 54 45  agesbooks.CREATE
```

- Payload size: `4c` → 76 bytes.
- RowID: `02` → 2.
- Record header size: `06` → 6 bytes.
- Serial types:
  - `17` → text 5 → `"index"`.
  - `2b` → text `(43 − 13) / 2 = 15` → `"idx_books_pages"`.
  - `17` → text 5 → `"books"` (`tbl_name`).
  - `01` → 8-bit int → `rootpage = 3`.
  - `65` → text `(101 − 13) / 2 = 44` → the `CREATE INDEX …` SQL.
- Values match: `"index"`, `"idx_books_pages"`, `"books"`, `3`, `"CREATE INDEX idx_books_pages\nON books(pages)"`.

Both cells confirm what `SELECT … FROM sqlite_schema` told me earlier. The schema table really is just rows in a regular table B-tree.

---

## 6. Page 2 — `books` Table B-Tree (file offset `0x1000`)

### Page header (`0x1000` — `0x1007`)

```
00001000: 0d 00 00 00 0c 0e e1 00 0f dc 0f c5 0f ae 0f 9c  ................
00001010: 0f 86 0f 72 0f 54 0f 3a 0f 29 0f 18 0f 00 0e e1  ...r.T.:.)......
```

- `0x1000` = `0d` → table leaf page.
- `0x1003 – 0x1004` = `00 0c` → **12 cells** (exactly the 12 rows I inserted).
- `0x1005 – 0x1006` = `0e e1` → cell content starts at page offset `0x0ee1` (file offset `0x1ee1`).

### Cell pointer array (`0x1008` — `0x101F`)

Pointers are stored in RowID order. Reading them out:

```
0f dc, 0f c5, 0f ae, 0f 9c, 0f 86, 0f 72, 0f 54, 0f 3a, 0f 29, 0f 18, 0f 00, 0e e1
```

So pointer[0] (RowID 1) points to page offset `0x0fdc` → file offset `0x1fdc`. Pointers grow towards higher addresses on the page but the cells they refer to are packed at the bottom — same pattern as page 1, just with more cells.

A useful sanity check: the *last* pointer (`0x0ee1`) equals the "start of cell content area" field. That is the cell that lives highest up in the unallocated region.

### Decoding Record 1 — Refactoring (file offset `0x1fdc`)

```
00001fd0: 6e 20 43 6f 64 65 01 d0 54 65 63 68 16 01 05 00  n Code..Tech....
                                            ^^ cell starts here
00001fe0: 23 02 15 52 65 66 61 63 74 6f 72 69 6e 67 01 c0  #..Refactoring..
00001ff0: 54 65 63 68 00 00 00 00 00 00 00 00 00 00 00 00  Tech............
```

Walking the bytes:

1. **Payload size**: `16` → 22 bytes.
2. **RowID**: `01` → 1.
3. **Record header size**: `05` → 5 bytes.
4. **Serial types** (4 columns: `id`, `title`, `pages`, `genre`):
   - `00` → NULL. This is the `id` column. Because `id` is `INTEGER PRIMARY KEY`, SQLite aliases it to the RowID and stores **nothing** in the record. Pure storage win.
   - `23` → text `(35 − 13) / 2 = 11` → `"Refactoring"`.
   - `02` → 16-bit signed integer (`pages`).
   - `15` → text `(21 − 13) / 2 = 4` → `"Tech"`.
5. **Values**:
   - `id`: stored implicitly via RowID = 1.
   - `title`: `52 65 66 61 63 74 6f 72 69 6e 67` → `"Refactoring"`.
   - `pages`: `01 c0` → `0x01c0 = 448`.
   - `genre`: `54 65 63 68` → `"Tech"`.

That is exactly the row I inserted. Doing this once for one row made every other row easy to read off the dump.

### Why the cells appear in reverse order in the dump

Cells are written **bottom-up** starting from the highest address. The first row I inserted (RowID 1, Refactoring) sits at the *highest* file offset (`0x1fdc`). The last row I inserted (RowID 12, Brave New World) sits at the lowest (`0x1ee1`). The cell pointer array indexes them in logical (RowID) order — so binary search by RowID still works even though the bytes on disk look reversed.

---

## 7. Page 3 — `idx_books_pages` Index B-Tree (file offset `0x2000`)

### Page header (`0x2000` — `0x2007`)

```
00002000: 0a 00 00 00 0c 0f a1 00 0f ed 0f e6 0f df 0f d8  ................
00002010: 0f d1 0f ca 0f c4 0f bd 0f b6 0f af 0f a8 0f a1  ................
```

- `0x2000` = `0a` → **Index Leaf Page**.
- Cell count: `00 0c` → 12 index entries (one per row).
- Cell content start: `0f a1` → file offset `0x2fa1`.

### Cell pointer array

```
0f ed, 0f e6, 0f df, 0f d8, 0f d1, 0f ca, 0f c4, 0f bd, 0f b6, 0f af, 0f a8, 0f a1
```

Notice the cells here are much **tighter** (mostly 6-byte entries) than on page 2. Index entries only store the indexed value(s) + the RowID, not the whole row.

### Decoding Index Cell 0 — smallest `pages` (`0x2fed`)

The index is sorted by `pages` ascending, then by RowID. The smallest `pages` value I inserted was `255` (Foundation, RowID 11). So cell 0 should encode `(255, 11)`.

```
00002fe0: 03 02 01 01 30 08 06 03 02 01 00 ff 0b 00 00 00  ....0...........
                                ^^ cell starts at 0x2fed
```

- Payload size: `06` → 6 bytes.
- Record header size: `03` → 3 bytes.
- Serial types:
  - `02` → 16-bit signed int (`pages`).
  - `01` → 8-bit signed int (RowID).
- Values:
  - `00 ff` → `0x00ff = 255` ✓ (Foundation has 255 pages).
  - `0b` → RowID `11` ✓.

### Decoding Index Cell 11 — largest `pages` (`0x2fa1`)

The largest `pages` value is `1312` (CLRS, RowID 9). That should be the last cell in the index — the one stored at the highest file offset.

```
00002fa0: 00 06 03 02 01 05 20 09 06 03 02 01 02 b0 04 06  ...... .........
              ^^ cell starts at 0x2fa1
```

- Payload size: `06`.
- Record header size: `03`.
- Serial types: `02` (16-bit int), `01` (8-bit int).
- Values: `05 20` → `0x0520 = 1312` ✓, `09` → RowID 9 ✓.

### A small SQLite optimization I noticed

Look at the third index cell, at `0x2fbd`:

```
00002fb0: 03 02 01 02 91 0a 06 03 02 01 01 f2 03 06 03 02  ................
00002fc0: 01 01 d0 02 05 03 02 09 01 c0 06 03 02 01 01 48  ...............H
                            ^^ cell at 0x2fbd
```

- Payload size: `05` → only 5 bytes (the others are 6).
- Record header size: `03`.
- Serial types: `02`, **`09`**.
- Values: `01 c0` → `0x01c0 = 448` (`pages`), and... nothing for the RowID.

Serial type `9` is the integer constant **1**. SQLite stored RowID 1 as just a *type code*, not a value — saving a byte. That matches my data: RowID 1 is Refactoring, whose `pages` is 448. Tiny but cool encoding trick.

---

## 8. How a `SELECT … WHERE pages = 448` actually executes

Putting all of the above together, here is what SQLite does for

```sql
SELECT * FROM books WHERE pages = 448;
```

1. The planner picks `idx_books_pages` (covers the predicate).
2. SQLite reads page 3 (the index leaf), binary-searches the cell pointer array on the indexed value `pages`, and lands on the cell with `(pages = 448, RowID = 1)`.
3. SQLite then reads page 2 (table leaf), binary-searches its cell pointer array by RowID, lands on pointer[0] = `0x0fdc`, and decodes the record there to get `('Refactoring', 448, 'Tech')`.
4. Result returned.

Two pages read, both indexed by binary search — no full scans. That is the entire point of having the index.

---

## 9. Takeaways

- **Pages over bytes**: SQLite never thinks in raw bytes — it pages the file into 4 KiB chunks and structures every chunk the same way (header → pointers → free space → cells from the bottom).
- **The schema is just a table**: `sqlite_schema` is a normal table-leaf B-tree on page 1. There is nothing magical about it.
- **Varints + serial types are dense**: small integers and short strings barely cost anything. Serial type `0` and serial type `9` are clever zero-byte encodings.
- **Index ↔ table separation**: index B-trees only store `(indexed_columns, RowID)` and rely on the table B-tree for the rest. Cheap to maintain, cheap to scan.
- **`xxd` + the file format spec is enough**: I did not need any special tool. Once I knew which offset to look at, the bytes just decoded into the things I expected.

What I want to dig into next: how SQLite handles overflow when a single row payload doesn't fit on a leaf page, and what `freelist trunk pages` actually look like after a `DELETE`.

---

## Files in this folder

- `create_library.sql` — schema + insert script I ran.
- `library.db` — the resulting 12 KiB database file (3 pages).
- `library.hex` — `xxd -g 1` dump of the database used in this writeup.
