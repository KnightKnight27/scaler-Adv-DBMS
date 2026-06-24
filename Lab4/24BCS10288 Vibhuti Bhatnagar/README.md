# Lab 4 — Reading a SQLite Database Byte-by-Byte

**Name:** Vibhuti Bhatnagar
**Role Number:** 24BCS10288
**Course:** Advanced DBMS — Scaler School of Technology

This lab takes a real `.db` file produced by SQLite 3 and walks through what every interesting byte means: the file header, page boundaries, B-tree internal vs leaf pages, cell-pointer arrays, varint encoding, and the journey a `SELECT … WHERE id = ?` query takes through the file.

All decoded values below were read out of `hexdump.txt` in this folder — no hand-waving.

---

## Contents of this folder

| File          | Purpose |
|---------------|---------|
| `seed.sql`    | SQL used to build `movies.db` — reproducible |
| `movies.db`   | The SQLite 3 database under analysis (48 KB, 12 pages) |
| `hexdump.txt` | `xxd movies.db` output (the artefact we are reading) |
| `README.md`   | This walkthrough |

### Why this schema?

A SQLite B-tree only acquires an **interior** (non-leaf) page when the root leaf overflows. To force that without a million rows, I gave every movie a deterministic ~1 100-byte `synopsis` so each 4 096-byte page holds only 3 rows. 25 rows then need 9 leaf pages plus one interior page on top — small enough to read by hand, structured enough to show every page type a `WHERE id = …` query touches.

---

## 0. How to reproduce

```bash
sqlite3 movies.db < seed.sql
xxd movies.db > hexdump.txt
sqlite3 movies.db "PRAGMA page_size; PRAGMA page_count;"
sqlite3 movies.db "SELECT name, pageno, pagetype, ncell FROM dbstat ORDER BY pageno;"
```

The last command (using SQLite's built-in `dbstat` virtual table) reports the *ground truth* for every page — useful to double-check what I'm about to decode from the raw hex.

---

## 1. The high-level map

Page numbers are 1-indexed; each page is 4096 bytes. The byte offset of any page is therefore `(page_no − 1) × 4096`.

| Page | Role                                     | Type       | Cells | Offset (hex) | Offset (dec) |
|-----:|------------------------------------------|------------|------:|--------------|--------------|
| 1    | `sqlite_schema` (always page 1)          | leaf       | 2     | `0x0000`     | 0            |
| 2    | `movies` root                            | **interior** | 8   | `0x1000`     | 4 096        |
| 3    | `studios` root                           | leaf       | 5     | `0x2000`     | 8 192        |
| 4    | `movies` leaf, rowids 1–3                | leaf       | 3     | `0x3000`     | 12 288       |
| 5    | `movies` leaf, rowids 4–6                | leaf       | 3     | `0x4000`     | 16 384       |
| 6    | `movies` leaf, rowids 7–9                | leaf       | 3     | `0x5000`     | 20 480       |
| 7    | `movies` leaf, rowids 10–12              | leaf       | 3     | `0x6000`     | 24 576       |
| 8    | `movies` leaf, rowids 13–15              | leaf       | 3     | `0x7000`     | 28 672       |
| 9    | `movies` leaf, rowids 16–18              | leaf       | 3     | `0x8000`     | 32 768       |
| 10   | `movies` leaf, rowids 19–21              | leaf       | 3     | `0x9000`     | 36 864       |
| 11   | `movies` leaf, rowids 22–24              | leaf       | 3     | `0xA000`     | 40 960       |
| 12   | `movies` leaf, rowid 25 (alone)          | leaf       | 1     | `0xB000`     | 45 056       |

Total: **12 × 4096 = 49 152 bytes**, which matches `ls -lh movies.db = 48 K`. The map was confirmed against `dbstat`.

---

## 2. The 100-byte file header (offset `0x0000`)

The first 100 bytes of every SQLite file are a global header. Pulling them from `hexdump.txt`:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0004 0000 000c  .....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
...
00000050: 0000 0000 0000 0000 0000 0000 0000 0004  ................
```

The fields I care about:

| Offset       | Bytes                                | Decoded               | Meaning                              |
|--------------|--------------------------------------|-----------------------|--------------------------------------|
| `0x00`–`0x0F`| `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `"SQLite format 3\0"` | Magic string                         |
| `0x10`–`0x11`| `10 00`                              | **4096**              | Page size                            |
| `0x12`       | `01`                                 | 1                     | File-format **write** version (legacy = rollback journal) |
| `0x13`       | `01`                                 | 1                     | File-format **read** version          |
| `0x14`       | `0c`                                 | 12                    | **Reserved bytes per page** — leaves only `4096 − 12 = 4084` bytes for content. (Confirms why `.dbinfo` printed *reserved bytes: 12*.) |
| `0x18`–`0x1B`| `00 00 00 04`                        | 4                     | File change counter (incremented every commit) |
| `0x1C`–`0x1F`| `00 00 00 0c`                        | **12**                | Database size, in pages              |
| `0x28`–`0x2B`| `00 00 00 02`                        | 2                     | Schema cookie                        |
| `0x2C`–`0x2F`| `00 00 00 04`                        | 4                     | Schema format number                 |
| `0x38`–`0x3B`| `00 00 00 01`                        | 1                     | Text encoding (1 = UTF-8)            |
| `0x5C`–`0x5F`| `00 00 00 04`                        | 4                     | `application_id` / version field     |

The byte `0x14 = 12` matters more than it looks. SQLite can leave a "reserved" tail on every page (used historically by encryption extensions). On this machine the default is **12 bytes**, so usable space per page is 4 084 bytes. We'll see this number again when we count free space inside a leaf.

---

## 3. Page 2 — the `movies` root, an **interior** B-tree node (offset `0x1000`)

```
00001000: 0500 0000 080f cc00 0000 000c 0fef 0fea  ................
00001010: 0fe5 0fe0 0fdb 0fd6 0fd1 0fcc 0000 0000  ................
```

### 3.1 Page header (12 bytes, because interior pages need a rightmost pointer)

| Byte(s)    | Hex            | Decoded       | Meaning                                                                         |
|------------|----------------|---------------|---------------------------------------------------------------------------------|
| `0x1000`   | `05`           | 5             | **Page type** — `0x05` = interior table B-tree (`0x0d` would be a leaf)         |
| `0x1001`–`0x1002` | `00 00` | 0             | Offset of first freeblock (none — page is freshly built)                        |
| `0x1003`–`0x1004` | `00 08` | **8**         | Number of cells on this page                                                    |
| `0x1005`–`0x1006` | `0f cc` | 0x0FCC = 4044 | Start of cell-content area (page-relative)                                      |
| `0x1007`   | `00`           | 0             | Fragmented free bytes                                                           |
| `0x1008`–`0x100B` | `00 00 00 0c` | **12** | Rightmost child pointer — the page that holds rowids *greater* than every key in this node |

### 3.2 Cell-pointer array (8 × 2 bytes, starting at `0x100C`)

```
0fef  0fea  0fe5  0fe0  0fdb  0fd6  0fd1  0fcc
```

The array is sorted **ascending by rowid**, so cell 0 holds the lowest-rowid pointer. Cells themselves are written from the end of the page *backwards*, which is why cell 0's content sits at the *highest* offset (`0x0FEF`) — see §3.3.

### 3.3 The eight interior cells

Each interior cell is `<4-byte child page>  <varint max-rowid>` — 5 bytes apiece in this layout because every rowid still fits in a 1-byte varint.

| Cell | Pointer (page-relative → file) | Raw bytes              | Child page | Max rowid |
|-----:|--------------------------------|------------------------|-----------:|----------:|
| 0    | `0x0FEF` → `0x1FEF`            | `00 00 00 04  03`      | **4**      | 3         |
| 1    | `0x0FEA` → `0x1FEA`            | `00 00 00 05  06`      | **5**      | 6         |
| 2    | `0x0FE5` → `0x1FE5`            | `00 00 00 06  09`      | **6**      | 9         |
| 3    | `0x0FE0` → `0x1FE0`            | `00 00 00 07  0c`      | **7**      | 12        |
| 4    | `0x0FDB` → `0x1FDB`            | `00 00 00 08  0f`      | **8**      | 15        |
| 5    | `0x0FD6` → `0x1FD6`            | `00 00 00 09  12`      | **9**      | 18        |
| 6    | `0x0FD1` → `0x1FD1`            | `00 00 00 0a  15`      | **10**     | 21        |
| 7    | `0x0FCC` → `0x1FCC`            | `00 00 00 0b  18`      | **11**     | 24        |

Rightmost pointer from the header → page **12** (the lone leaf holding rowid 25).

### 3.4 What this node "means"

Reading the eight cells plus the rightmost pointer gives a navigation rule:

```
rowid ≤ 3   → page 4
rowid ≤ 6   → page 5
rowid ≤ 9   → page 6
rowid ≤ 12  → page 7
rowid ≤ 15  → page 8
rowid ≤ 18  → page 9
rowid ≤ 21  → page 10
rowid ≤ 24  → page 11
rowid > 24  → page 12   (rightmost pointer)
```

So this single interior page replaces what would otherwise be a linear walk over nine separate leaves.

---

## 4. The traversal of `SELECT * FROM movies WHERE id = 14`

This is the whole point of the B-tree, walked step by step out of the file.

1. **Page 1** (sqlite_schema) is read to learn that `movies.rootpage = 2`.
2. SQLite seeks to `(2 − 1) × 4096 = 0x1000`. First byte = `0x05` → interior page, so it walks the cell-pointer array:
   * cell 0 max-rowid = 3 → `14 > 3`  ✗
   * cell 1 max-rowid = 6 → `14 > 6`  ✗
   * cell 2 max-rowid = 9 → `14 > 9`  ✗
   * cell 3 max-rowid = 12 → `14 > 12` ✗
   * cell 4 max-rowid = 15 → `14 ≤ 15` ✓  follow child page = **8**
3. Seek to `(8 − 1) × 4096 = 0x7000`. First byte = `0x0d` → leaf. Scan its three cell pointers for the cell whose payload rowid equals 14.

Two reads, no full-table scan. The depth-1 tree we have here means lookups bottom out in one page hop; a million-row table would have a depth-3 tree and still only need three or four page reads.

---

## 5. Page 4 — a `movies` leaf in detail (offset `0x3000`)

```
00003000: 0d00 0000 0302 d900 0b97 0739 02d9 0000  ...........9....
```

### 5.1 Page header (8 bytes — leaves don't carry a rightmost pointer)

| Byte(s)    | Hex     | Decoded       | Meaning                              |
|------------|---------|---------------|--------------------------------------|
| `0x3000`   | `0d`    | 13            | **Leaf** table B-tree                |
| `0x3001`–`0x3002` | `00 00` | 0     | No freeblocks                        |
| `0x3003`–`0x3004` | `00 03` | **3** | Three cells on this leaf (rowids 1, 2, 3) |
| `0x3005`–`0x3006` | `02 d9` | 0x02D9 = 729 | Cell content starts 729 bytes into the page |
| `0x3007`   | `00`    | 0             | No fragmented free bytes             |

### 5.2 Cell-pointer array

```
0b97   0739   02d9
```

| Cell | Pointer | File offset | Holds rowid |
|-----:|---------|-------------|-------------|
| 0    | `0x0B97` | `0x3B97`   | 1 — "Inception"        |
| 1    | `0x0739` | `0x3739`   | 2 — "The Matrix"       |
| 2    | `0x02D9` | `0x32D9`   | 3 — "Interstellar"     |

### 5.3 Free-space sanity check

Page size minus reserved = `4096 − 12 = 4084` usable bytes.
Used: `8` (header) `+ 6` (pointer array) `+ (4084 − 729)` (cell area) = `8 + 6 + 3355 = 3369` bytes.
Remaining free space = `4084 − 3369 = 715` bytes — which is exactly what `dbstat` reported (`unused = 715` on page 4). ✓

That's a meaningful cross-check: we just verified the page layout arithmetic from first principles.

---

## 6. Decoding row 1 — "Inception"

The first cell sits at file offset `0x3B97`. From `hexdump.txt`:

```
00003b90: 3030 3030 3030 3088 5a01 0500 1f91 2549  0000000.Z.....%I
00003ba0: 6e63 6570 7469 6f6e 4d30 3120 3030 3030  nceptionM01 0000
```

So starting at `0x3B97`:

```
88 5a   01   05   00   1f   91 25   49 6e 63 65 70 74 69 6f 6e   4d 30 31 20 ...
└────┘ └──┘ └──┘ └──┘ └──┘ └────┘   └──────── "Inception" ───┘   └ synopsis  ┘
 size  rowid hdr  T_id  T_t  T_syn       (text payload of title)  (text payload)
```

### 6.1 Varint refresher

A SQLite varint takes up to 9 bytes. The low 7 bits of each byte hold data; the high bit is a *continue* flag. To decode:

```
while (byte & 0x80) shift previous result left 7, OR in low 7 bits, read next byte
the final byte (high bit 0) contributes its full 7 bits
```

* `88 5a` → `0x88` has the continue bit set, contributes `0x08` (low 7 bits = `0b0001000`).
  Result so far = `8`. Next byte `0x5A` (no continue) = `90`. Final = `8 << 7 | 90` = `1114`.
  **Payload length: 1114 bytes.**

* `91 25` → `(0x11 << 7) | 0x25 = 17·128 + 37 = 2213`. (Used in §6.2 to size the `synopsis` column.)

The other four bytes (`01`, `05`, `00`, `1f`) all have the high bit clear, so they are 1-byte varints worth 1, 5, 0, and 31 respectively.

### 6.2 The record header

After payload size and rowid, every leaf cell begins with a **record header** that describes each column's serial type:

| Byte(s) | Decoded             | Column           | Meaning |
|---------|---------------------|------------------|---------|
| `05`    | header is 5 bytes long | —             | Tells the reader where the body starts |
| `00`    | serial type `0`     | `id`             | NULL — `id` is `INTEGER PRIMARY KEY`, so SQLite stores the value **only in the rowid varint**, not the row body. Saves bytes. |
| `1f`    | serial type `31`    | `title`          | TEXT, byte length = `(31 − 13) / 2 = 9` → "Inception" |
| `91 25` | serial type `2213`  | `synopsis`       | TEXT, byte length = `(2213 − 13) / 2 = 1100` → 1100-char synopsis |

### 6.3 TEXT / BLOB serial types

SQLite encodes string and blob lengths *into the serial type itself*:

* `serial_type = 2 × byte_length + 13` → **TEXT**
* `serial_type = 2 × byte_length + 12` → **BLOB**
* `0–9` are fixed widths (NULL, INT8…INT64, IEEE-754 double, etc.)

So a TEXT serial type is always **odd ≥ 13**, a BLOB is always **even ≥ 12**, and a single subtraction-and-shift recovers the length.

### 6.4 Putting it all together

* Payload size declared: 1114 bytes.
* Calculated from columns: `0` (id, stored in rowid) `+ 9` (title) `+ 1100` (synopsis) `+ 5` (record-header bytes) = `1114`. ✓
* Rowid: 1.
* Title bytes at `0x3BA0` onward: `49 6e 63 65 70 74 69 6f 6e` → "Inception". ✓

The same template applies to every other leaf cell — only the bytes change.

---

## 7. Page 12 — the smallest, weirdest leaf (offset `0xB000`)

```
0000b000: 0d00 0000 010b 8e00 0b8e 0000 0000 0000  ................
```

| Field | Value |
|-------|-------|
| Page type | `0x0d` (leaf) |
| Number of cells | **1** (only rowid 25 lives here) |
| Cell content start | `0x0B8E` = 2958 |
| Free space | `4084 − 8 − 2 − (4084 − 2958) = 2948` bytes — confirmed by `dbstat.unused = 2948`. |

Page 12 is mostly empty because rowid 25 spilled over from page 11 during the last split. If we ever insert rowids 26, 27, … they would land on page 12 first, only triggering another split when this page itself fills up. **Half-empty trailing leaves are normal after a balanced split** — SQLite does not aggressively compact.

---

## 8. What I actually learned

* A SQLite database is a **flat array of fixed-size pages**. There is no "table file"; everything — schema, table heap, indexes, journals — lives inside this array, addressed by page number.
* Each page is a self-describing little world: an 8- or 12-byte header, a cell-pointer array growing downward, and cells growing upward from the bottom. The gap between them *is* the free space.
* **Interior pages are routers**, not storage. They carry only `(child page, max-rowid)` pairs plus a rightmost pointer.
* **Leaves carry the data**, encoded with a record header that names each column's *serial type*. The payload size and rowid are themselves varints, so SQLite spends only as many bytes as the value needs.
* **`INTEGER PRIMARY KEY` columns are free** — they appear as serial type `0` in the row body and are reconstructed from the rowid. That's why every cell in §6 had its `id` field stored as a single `0x00`.
* A `SELECT … WHERE id = N` lookup is `O(log n)` page reads — *exactly* one read per tree level. On this 25-row database that's 2 reads; a multi-million-row table would need 3 or 4.
* The byte-level layout *matches* what `PRAGMA page_count`, `.dbinfo`, and `dbstat` report — useful when something looks wrong, because you can drop down to the dump and prove what's actually on disk.

---

## 9. Quick reference — commands used

```bash
# Build
sqlite3 movies.db < seed.sql

# Generate dump
xxd movies.db > hexdump.txt

# Sanity-check the layout decoded above
sqlite3 movies.db "PRAGMA page_size;"        # 4096
sqlite3 movies.db "PRAGMA page_count;"       # 12
sqlite3 movies.db ".dbinfo"
sqlite3 movies.db "SELECT name, pageno, pagetype, ncell, payload, unused
                   FROM dbstat ORDER BY pageno;"
```
