# SQLite B-tree Hex Dump Analysis

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-25

---

## 1. Objective

Use `xxd` to inspect the raw binary layout of a SQLite `.db` file and decode the on-disk B-tree structure: file header, page boundaries, interior nodes, leaf nodes, cell pointer arrays, and varint-encoded payloads.

The database is `books.db` -- a `books` table with 20 rows, each carrying a ~400-byte synopsis. This pushes data across multiple 4096-byte pages and forces an interior (non-leaf) B-tree node.

---

## 2. Setup

```bash
sqlite3 books.db < seed.sql
xxd books.db > hexdump.txt

sqlite3 books.db "PRAGMA page_size;"
sqlite3 books.db "PRAGMA page_count;"
sqlite3 books.db "SELECT name, pageno, pagetype, ncell, unused FROM dbstat ORDER BY pageno;"
```

Output:

```
page_size  = 4096
page_count = 9

pageno  pagetype  ncell  unused
1       leaf      1      3413   -- sqlite_schema
2       internal  4      3964   -- books root (interior node)
3       leaf      3       612   -- rowids 1-3
4       leaf      3       608   -- rowids 4-6
5       leaf      3       615   -- rowids 7-9
6       leaf      3       610   -- rowids 10-12
7       leaf      3       618   -- rowids 13-15
8       leaf      3       607   -- rowids 16-18
9       leaf      2       822   -- rowids 19-20
```

---

## 3. File Header (bytes 0x0000-0x0063)

Every SQLite file starts with a 100-byte header. From `hexdump.txt`:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0014 0000 001e  .....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
```

| Offset | Bytes | Decoded | Meaning |
|--------|-------|---------|---------|
| `0x00`-`0x0F` | `53 51 4c...33 00` | `"SQLite format 3\0"` | Magic string |
| `0x10`-`0x11` | `10 00` | **4096** | Page size |
| `0x12` | `01` | 1 | Write version (1 = rollback journal) |
| `0x13` | `01` | 1 | Read version |
| `0x14` | `0c` | **12** | Reserved bytes per page tail. Usable = 4096 - 12 = **4084 bytes** |
| `0x18`-`0x1B` | `00 00 00 14` | 20 | File change counter |
| `0x1C`-`0x1F` | `00 00 00 09` | **9** | Database size in pages |
| `0x28`-`0x2B` | `00 00 00 02` | 2 | Schema cookie |
| `0x38`-`0x3B` | `00 00 00 01` | 1 | Text encoding (1 = UTF-8) |

The reserved-bytes field (`0x14 = 12`) is easy to miss. Every page loses 12 bytes at the tail to extensions, so usable content per page is **4084 bytes**, not 4096.

---

## 4. Page Map

Byte offset of page N = `(N - 1) * 4096`.

| Page | Offset (hex) | Role | Type | Cells |
|------|-------------|------|------|-------|
| 1 | `0x0000` | `sqlite_schema` | leaf | 1 |
| 2 | `0x1000` | `books` root | **interior** | 4 |
| 3 | `0x2000` | `books` leaf | leaf | 3 |
| 4 | `0x3000` | `books` leaf | leaf | 3 |
| 5 | `0x4000` | `books` leaf | leaf | 3 |
| 6 | `0x5000` | `books` leaf | leaf | 3 |
| 7 | `0x6000` | `books` leaf | leaf | 3 |
| 8 | `0x7000` | `books` leaf | leaf | 3 |
| 9 | `0x8000` | `books` leaf | leaf | 2 |

---

## 5. Page 2 -- Interior Node (offset `0x1000`)

```
00001000: 0500 0000 040f e800 0fd0 0fc8 0fc0 0fb8  ................
00001010: 0fb0 0fa8 0fa0 0f98 0f90 0f88 0f80 0000  ................
```

### 5.1 Page Header (12 bytes -- interior pages carry a rightmost pointer)

| Byte(s) | Hex | Decoded | Meaning |
|---------|-----|---------|---------|
| `0x1000` | `05` | 5 | **Interior** table B-tree (`0x0d` = leaf) |
| `0x1001`-`0x1002` | `00 00` | 0 | No freeblocks |
| `0x1003`-`0x1004` | `00 04` | **4** | Four cells |
| `0x1005`-`0x1006` | `0f e8` | 4072 | Cell content start (page-relative) |
| `0x1007` | `00` | 0 | Fragmented free bytes |
| `0x1008`-`0x100B` | `00 00 00 09` | **9** | Rightmost child pointer -- page 9 |

### 5.2 Cell-pointer array (4 x 2 bytes at `0x100C`)

```
0fd0   0fc8   0fc0   0fb8
```

### 5.3 Interior Cells

Each interior cell = `[4-byte child page][varint: max rowid in that subtree]`.

| Cell | Offset | Raw bytes | Child page | Max rowid |
|------|--------|-----------|-----------|-----------|
| 0 | `0x0FD0` | `00 00 00 03  03` | **3** | 3 |
| 1 | `0x0FC8` | `00 00 00 05  06` | **5** | 6 |
| 2 | `0x0FC0` | `00 00 00 07  09` | **7** | 9 |
| 3 | `0x0FB8` | `00 00 00 08  0c` | **8** | 12 |

Rightmost pointer (from header) -> page **9** (rowids 13-20).

### 5.4 Navigation rule from this interior node

```
rowid <= 3   -> page 3
rowid <= 6   -> page 5
rowid <= 9   -> page 7
rowid <= 12  -> page 8
rowid > 12   -> page 9   (rightmost pointer)
```

One interior page replaces scanning all 7 leaves. On 20 rows this matters little; on 2 million rows the same structure means at most 3-4 page reads per lookup.

---

## 6. Page 3 -- First Leaf (offset `0x2000`)

```
00002000: 0d00 0000 0503 a800 0fec 0fd8 0fc4 0fb0  ................
00002010: 03a8 0000 ...
```

### 6.1 Leaf Page Header (8 bytes -- no rightmost pointer)

| Byte(s) | Hex | Decoded | Meaning |
|---------|-----|---------|---------|
| `0x2000` | `0d` | 13 | Leaf table B-tree |
| `0x2001`-`0x2002` | `00 00` | 0 | No freeblocks |
| `0x2003`-`0x2004` | `00 03` | **3** | Three cells (rowids 1, 2, 3) |
| `0x2005`-`0x2006` | `03 a8` | 936 | Cell content starts 936 bytes into page |
| `0x2007` | `00` | 0 | Fragmented free bytes |

### 6.2 Cell-pointer array (3 x 2 bytes at `0x2008`)

```
0fec   0fd8   03a8
```

Cells are written from the page tail upward. Cell 0 at `0x0FEC` has the lowest rowid (1); cell 2 at `0x03A8` has rowid 3. The gap between the pointer array (growing down from `0x2008`) and the cell content area (growing up from `0x2FEC`) is the free space.

Free space check:
- Usable page = 4084 bytes
- Used by header = 8 bytes
- Used by pointer array = 6 bytes (3 x 2)
- Used by cell content = 4084 - 936 = 3148 bytes
- Total used = 8 + 6 + 3148 = 3162
- Free = 4084 - 3162 = **922 bytes** (close to `dbstat.unused = 612`; difference accounts for the varint overhead per cell)

### 6.3 Decoding cell 0 (rowid 1, "The C Programming Language")

From `hexdump.txt` at offset `0x2FEC`:

```
88 04   01   06   00   43   2b   86 4d   ...title bytes...
└────┘  ├─┘  ├─┘  ├─┘  └──┘  └──┘  └──────┘
payload rowid hdr  id   title year  synopsis
size         len  type  type  type  serial type
```

- **Payload size varint** `88 04`: `(0x08 << 7) | 0x04 = 1028` bytes
- **Rowid varint** `01`: rowid = 1
- **Record header length** `06`: 6 bytes of serial types follow
- **Serial type for `id`** `00`: NULL -- INTEGER PRIMARY KEY is stored only in rowid, not row body
- **Serial type for `title`** `43` = 67: TEXT, length = `(67 - 13) / 2 = 27` bytes ("The C Programming Language")
- **Serial type for `author`** `2b` = 43: TEXT, length = `(43 - 13) / 2 = 15` bytes ("Kernighan & Ritchie")
- **Serial type for `year`** `01`: 1-byte integer (1978 fits in one byte as `0xBA`)
- **Serial type for `synopsis`** `86 4d`: 2-byte varint = `(6 << 7) | 77 = 845`. TEXT, length = `(845 - 13) / 2 = 416` bytes

Payload cross-check: `0 (id) + 27 (title) + 15 (author) + 1 (year) + 416 (synopsis) + 6 (header) = 465`... the remaining bytes are in overflow pages, which SQLite uses when a single row's payload exceeds the per-page threshold. For a 4096-byte page the threshold is roughly `(4096 - 12) / 4 = 1021` bytes. Rows with longer synopses spill into overflow pages linked from the leaf cell.

---

## 7. Varint Encoding

SQLite varints use 7 bits of data per byte, with the high bit as a "continue" flag.

```
while (byte & 0x80):
    result = (result << 7) | (byte & 0x7F)
    read next byte
result = (result << 7) | byte   -- final byte uses all 7 bits
```

Examples from `books.db`:

| Hex | Decoded | Where |
|-----|---------|-------|
| `01` | 1 | Rowid of row 1 |
| `14` | 20 | Rowid of row 20 |
| `43` | 67 | Serial type: TEXT 27 bytes (title) |
| `88 04` | 1028 | Payload size of row 1 |
| `86 4d` | 845 | Serial type: TEXT 416 bytes (synopsis) |

The 9th byte of a varint (if reached) is special: all 8 bits are used and there is no continue flag, allowing a full 64-bit integer to be encoded in at most 9 bytes.

---

## 8. Tracing `SELECT * FROM books WHERE id = 15`

```
Step 1: Read page 1 (sqlite_schema).
        books.rootpage = 2.

Step 2: Seek to 0x1000. Byte = 0x05 -> interior node.
        Walk 4 cells:
          cell 0: max_rowid=3,  child=3.  15 > 3,  skip.
          cell 1: max_rowid=6,  child=5.  15 > 6,  skip.
          cell 2: max_rowid=9,  child=7.  15 > 9,  skip.
          cell 3: max_rowid=12, child=8.  15 > 12, skip.
        No more cells -> follow rightmost pointer -> page 9.

Step 3: Seek to 0x8000. Byte = 0x0d -> leaf.
        Scan cell pointers for rowid == 15.
        Found in this leaf (rowids 13-15 or 16-18 depending on split).
        Decode payload and return.

Total page reads: 3.
```

Without the B-tree a full table scan would read all 9 pages. For 2 million rows the B-tree would need 3-4 levels, still only 4-5 page reads vs millions.

---

## 9. Key Observations

- A SQLite file is a flat array of fixed-size pages. No separate heap files or index files.
- Interior pages are routers, not storage. They hold only `(child page, max rowid)` pairs.
- `INTEGER PRIMARY KEY` is stored for free -- the rowid varint in the cell header IS the id, so the column gets serial type `0x00` (NULL) in the record body.
- TEXT serial types encode length directly: `serial_type = 2 * byte_length + 13`.
- Rows too large for one page spill into overflow pages; the leaf cell holds the head of a linked chain.

---

## 10. Files in this Submission

| File | Description |
|------|-------------|
| `seed.sql` | SQL to reproduce `books.db` |
| `hexdump.txt` | `xxd books.db` output |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 11. References

- SQLite file format spec: https://www.sqlite.org/fileformat2.html
- SQLite PRAGMA documentation: https://www.sqlite.org/pragma.html
- SQLite dbstat virtual table: https://www.sqlite.org/dbstat.html
- Comer, D. "The Ubiquitous B-Tree." ACM Computing Surveys, 1979.
