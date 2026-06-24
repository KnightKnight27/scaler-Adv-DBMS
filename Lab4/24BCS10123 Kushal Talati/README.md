# Lab 4 — Walking a SQLite Database Byte-by-Byte

**Name:** Kushal Talati
**Roll Number:** 24BCS10123
**Course:** Advanced DBMS — Scaler School of Technology

This write-up takes a small but non-trivial SQLite 3 database (`recipes.db`) and reads it the way the engine does — byte by byte, page by page. Everything below was decoded directly out of `hexdump.txt` in this folder; nothing was paraphrased from documentation. Where I do quote a number that came from `dbstat`, I say so and then re-derive it from the raw bytes to make sure the math closes.

The database was built with `page_size = 8192` on purpose. Most reference walkthroughs (including a couple of other Lab 4 submissions in this repo) use the 4096-byte default, so almost every offset, cell count, and free-space arithmetic in this file is different — which forced me to verify each step instead of pattern-matching.

---

## Files in this folder

| File         | What it is |
|--------------|------------|
| `seed.sql`   | The full SQL needed to rebuild `recipes.db` from scratch. Uses a CTE so the 32 inserts stay readable. |
| `recipes.db` | The 8 KB-page SQLite database under analysis (90 112 bytes, 11 pages). |
| `hexdump.txt`| `xxd recipes.db` output. This is the artefact every section below decodes. |
| `README.md`  | This walkthrough. |

---

## Why this schema produces a useful B-tree

I want the `recipes` table to have **more than one leaf page** so the database has to grow an interior (router) page on top — that is the whole point of the lab. Two design choices fall out of that:

1. **Every recipe gets exactly 1 500 bytes of `instructions`.** The text is built by a `WITH … SELECT` in `seed.sql` so the file stays readable. At 1 500 bytes of TEXT plus a tiny title and cuisine, each cell is around 1 521 bytes — which means each 8 192-byte page can hold about **5** recipes before it splits.
2. **32 recipes across 8 cuisines.** 32 ÷ 5 ≈ 7 leaves, exactly enough to force one interior page above them but not so many that the tree becomes deep. The shape ends up being a depth-1 B-tree with 7 leaves and one interior root.

The `cuisines` table is tiny (8 rows) and exists for one extra reason: I declared `name` as `UNIQUE`, which makes SQLite quietly create an internal index page. That auto-index is something PR 410's `movies/studios` schema doesn't have, and it shows up nicely in §5.

---

## How to reproduce

```bash
sqlite3 recipes.db < seed.sql
xxd recipes.db > hexdump.txt

# Ground-truth checks (everything below is re-derived from raw hex):
sqlite3 recipes.db "PRAGMA page_size;"        # 8192
sqlite3 recipes.db "PRAGMA page_count;"       # 11
sqlite3 recipes.db "SELECT name, pageno, pagetype, ncell, payload, unused
                    FROM dbstat ORDER BY pageno;"
```

---

## 1. The page map

Pages are 1-indexed and each is 8 192 bytes, so the file offset of page *p* is `(p − 1) × 0x2000`.

| Page | Lives at (hex) | Role                                  | Type     | Cells | Notes |
|-----:|----------------|---------------------------------------|----------|------:|-------|
| 1    | `0x00000`      | `sqlite_schema` (always page 1)       | leaf     | 3     | One row per CREATE TABLE plus one for the UNIQUE auto-index. |
| 2    | `0x02000`      | `cuisines` table                      | leaf     | 8     | Fits in a single page — never split. |
| 3    | `0x04000`      | `sqlite_autoindex_cuisines_1`         | leaf     | 8     | The index SQLite created behind `UNIQUE (name)`. |
| 4    | `0x06000`      | `recipes` **root** (interior)         | interior | 6     | Routes lookups to pages 5–11 (see §6). |
| 5    | `0x08000`      | `recipes` leaf — rowids 1–5           | leaf     | 5     | |
| 6    | `0x0A000`      | `recipes` leaf — rowids 6–10          | leaf     | 5     | |
| 7    | `0x0C000`      | `recipes` leaf — rowids 11–15         | leaf     | 5     | |
| 8    | `0x0E000`      | `recipes` leaf — rowids 16–20         | leaf     | 5     | Decoded in detail in §7. |
| 9    | `0x10000`      | `recipes` leaf — rowids 21–25         | leaf     | 5     | |
| 10   | `0x12000`      | `recipes` leaf — rowids 26–30         | leaf     | 5     | |
| 11   | `0x14000`      | `recipes` leaf — rowids 31–32         | leaf     | 2     | Half-empty tail leaf, see §8. |

Total: 11 × 8 192 = 90 112 bytes, which matches `ls -l recipes.db`. The map agrees with `dbstat` for every page.

---

## 2. The 100-byte file header

The first 100 bytes of any SQLite file are the global header. The relevant slices, pulled straight from `hexdump.txt`:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 2000 0101 0c40 2020 0000 0001 0000 000b   ....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
00000050: 0000 0000 0000 0000 0000 0000 0000 0001  ................
```

| Offset       | Bytes                | Decoded         | Meaning                                                  |
|--------------|----------------------|-----------------|----------------------------------------------------------|
| `0x00`–`0x0F`| `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `"SQLite format 3\0"` | Magic string — every SQLite file starts with these 16 bytes. |
| `0x10`–`0x11`| `20 00`              | **8 192**       | Page size in bytes. Big-endian 16-bit; the value `1` (`0x0001`) is a sentinel that means 65 536. |
| `0x12`       | `01`                 | 1               | Write format version (1 = legacy / rollback journal). |
| `0x13`       | `01`                 | 1               | Read format version. |
| `0x14`       | `0c`                 | **12**          | Reserved bytes per page. Usable space per page is therefore `8192 − 12 = 8180` bytes — this number is the basis of every free-space check below. |
| `0x15`       | `40`                 | 64              | Max embedded payload fraction (×255/256). |
| `0x16`       | `20`                 | 32              | Min embedded payload fraction. |
| `0x17`       | `20`                 | 32              | Leaf payload fraction. |
| `0x18`–`0x1B`| `00 00 00 01`        | 1               | File change counter — first (and only) commit. |
| `0x1C`–`0x1F`| `00 00 00 0b`        | **11**          | Database size in pages. Matches `PRAGMA page_count`. |
| `0x28`–`0x2B`| `00 00 00 02`        | 2               | Schema cookie (incremented on DDL). |
| `0x2C`–`0x2F`| `00 00 00 04`        | 4               | Schema format. |
| `0x38`–`0x3B`| `00 00 00 01`        | 1               | Text encoding — 1 = UTF-8. |
| `0x5C`–`0x5F`| `00 00 00 01`        | 1               | SQLite version-valid-for counter. |

Two things to keep in mind from this header for the rest of the document: **page size = 8 192** and **reserved = 12**, so when I check free space later I'll subtract 12 from 8 192.

---

## 3. The shape of a page (a one-screen refresher)

Every page in a SQLite table B-tree has the same shape, regardless of whether it stores routing info or actual rows:

```
┌──────────────────────────────────────────────────────────────┐ offset 0
│ 8-byte page header   (12 bytes if interior — extra child ptr)│
├──────────────────────────────────────────────────────────────┤
│ cell-pointer array, 2 bytes per cell, grows downward         │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│                  ←—— free space ——→                          │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│ cells, packed from the bottom upward                         │
└──────────────────────────────────────────────────────────────┘ offset 8180 (usable end; 12 reserved bytes follow)
```

That layout shows up in every page in §§4–8 below — only the byte counts change.

---

## 4. Page 1 — `sqlite_schema`

Page 1 is special: the 100-byte file header eats into its top, so its B-tree page header starts at **offset 100**, not 0. Bytes 0x64 onward:

```
00000060: 002e 8df8 0d1f ec00 031e 8400 1f37 1fbb  .............7..
00000070: 1e87 ...
```

| Field                | Bytes (at offset) | Value         | Meaning |
|----------------------|-------------------|---------------|---------|
| Page type            | `0x64` → `0d`     | 13 = leaf table | Schema rows are stored as ordinary rows. |
| First freeblock      | `0x65`–`0x66` → `1f ec` | 0x1FEC = 8172 | **There IS a freeblock** here — see below. |
| Cell count           | `0x67`–`0x68` → `00 03` | 3 | One row per `CREATE TABLE` + one for the auto-index from §5. |
| Cell-content start   | `0x69`–`0x6A` → `1e 84` | 0x1E84 = 7812 | Cells live high in the page. |
| Fragmented bytes     | `0x6B` → `00`     | 0 | |
| Cell pointers begin  | `0x6C` …          | `1f 37`, `1f bb`, `1e 87` | Three pointers into the page-content area. |

The freeblock at offset 0x1FEC is interesting: it exists because the autoindex row (page 3) was registered after the two table rows, and SQLite shuffled cells inside page 1 to keep them ordered by rowid. The shuffle left a small hole, which is exactly what a freeblock represents. (`dbstat` reports `unused = 7706` for page 1, which is the combined free space, including this freeblock.)

I do not decode the schema rows themselves byte-by-byte — they are just TEXT/INTEGER columns describing the `CREATE TABLE …` statements — but the SQL text for each one is plainly visible in the dump if you scroll the hex window to around offset `0x1E84`.

---

## 5. Page 3 — the hidden index that nobody asked for

When I declared `UNIQUE (name)` on `cuisines`, SQLite silently created an **index** to enforce that constraint. That index lives on page 3 with rootpage 3 and the auto-assigned name `sqlite_autoindex_cuisines_1`. From `dbstat`:

```
sqlite_autoindex_cuisines_1 | 3 | leaf | 8 | 84 | 8064
```

Eight cells, 84 bytes of payload total — meaning the index is keyed by `name` (the unique column) plus the rowid pointer back into the `cuisines` table. The bytes at `0x4000` look like a standard leaf, just one with very small cells:

```
00004000: 0a00 1ffb 0008 1ff5 1fed 1fe8 1fe3 1fdb  ................
00004010: 1fd2 1fcb ...
```

The first byte is `0x0a`, which means **index leaf** (table leaves use `0x0d`). The next interesting byte is `0x08` at offset 0x4004 — the cell count, 8, one cell per cuisine.

I'm leaving the index decode at this depth because the lab brief is about the table B-tree, but the existence of this page is the answer to a question every Lab 4 walkthrough should ask once: *"What are the pages I didn't ask for?"* In this database, the answer is page 3.

---

## 6. Page 4 — the `recipes` root, an interior B-tree node

This is the page that makes the database an actual tree instead of a heap. Bytes at `0x6000`:

```
00006000: 0500 0000 061f d600 0000 000b 1fef 1fea  ................
00006010: 1fe5 1fe0 1fdb 1fd6 0000 0000 0000 0000  ................
```

### 6.1 The 12-byte interior page header

| Offset (file)  | Bytes          | Value         | Meaning |
|----------------|----------------|---------------|---------|
| `0x6000`       | `05`           | 5             | **Interior table B-tree** (leaf would be `0x0d`). |
| `0x6001`–`0x6002` | `00 00`     | 0             | No freeblocks. |
| `0x6003`–`0x6004` | `00 06`     | **6**         | Six routing cells on this page. |
| `0x6005`–`0x6006` | `1f d6`     | 0x1FD6 = 8150 | Cell-content area starts 8 150 bytes into the page. |
| `0x6007`       | `00`           | 0             | Fragmented free bytes. |
| `0x6008`–`0x600B` | `00 00 00 0b` | **11**     | **Rightmost child pointer** — the page that holds rowids greater than every key in this node. Interior pages need this because the cells themselves only cover ranges up to the largest key. |

### 6.2 Cell-pointer array (6 × 2 bytes, starting at `0x600C`)

```
1fef  1fea  1fe5  1fe0  1fdb  1fd6
```

Cells are stored at the bottom of the page in **rowid-ascending order from cell 0 onward**, which means cell 0 sits at the *highest* page-relative offset. So I'll walk them from `0x1FEF` down.

### 6.3 The six interior cells

An interior cell is `<4-byte BE child page>  <varint largest-rowid-in-that-child>`. Every largest-rowid here still fits in one varint byte, so each cell is 5 bytes. Reading the bottom of the page (file offsets `0x7FCC`–`0x7FF3`):

```
00007fd0: 0000 0000 0000 0000 000a 1e00 0000 0919  ................
00007fe0: 0000 0008 1400 0000 070f 0000 0006 0a00  ................
00007ff0: 0000 0505 0000 0000 ...
```

| Cell | Page-rel offset | File offset | Bytes                | Child page | Max rowid in that child |
|-----:|-----------------|-------------|----------------------|-----------:|------------------------:|
| 0    | `0x1FEF`        | `0x7FEF`    | `00 00 00 05  05`    | **5**      |  5 |
| 1    | `0x1FEA`        | `0x7FEA`    | `00 00 00 06  0a`    | **6**      | 10 |
| 2    | `0x1FE5`        | `0x7FE5`    | `00 00 00 07  0f`    | **7**      | 15 |
| 3    | `0x1FE0`        | `0x7FE0`    | `00 00 00 08  14`    | **8**      | 20 |
| 4    | `0x1FDB`        | `0x7FDB`    | `00 00 00 09  19`    | **9**      | 25 |
| 5    | `0x1FD6`        | `0x7FD6`    | `00 00 00 0a  1e`    | **10**     | 30 |
| —    | (in header)     | —           | rightmost            | **11**     | (rowids > 30) |

This is the routing rule SQLite will execute when you ask it for any specific rowid:

```
rowid ≤  5  → page  5
rowid ≤ 10  → page  6
rowid ≤ 15  → page  7
rowid ≤ 20  → page  8
rowid ≤ 25  → page  9
rowid ≤ 30  → page 10
rowid > 30  → page 11   (rightmost pointer)
```

### 6.4 Free-space sanity check on this page

Usable bytes per page: `8192 − 12 = 8180`. On this page:

* header: 12 bytes
* cell-pointer array: 6 × 2 = 12 bytes
* six 5-byte cells = 30 bytes packed at the bottom (`0x1FD6`–`0x1FFA`)

So free space = `8180 − 12 − 12 − 30 = 8126` bytes. `dbstat.unused = 8126`. The math closes. ✓

---

## 7. Tracing `SELECT * FROM recipes WHERE id = 18` through the file

This is the whole reason the B-tree exists. The recipe with rowid 18 is "Tempura". Walking the lookup step-by-step out of the raw dump:

1. SQLite reads page 1 (it always knows page 1 is the schema page) and finds the row for table `recipes`. Its `rootpage` column is **4**.
2. Seek to `(4 − 1) × 8192 = 0x6000`. First byte is `0x05`, so this is an interior page. Scan its cell-pointer array:
   * cell 0 max-rowid = 5 → `18 > 5`  ✗
   * cell 1 max-rowid = 10 → `18 > 10` ✗
   * cell 2 max-rowid = 15 → `18 > 15` ✗
   * cell 3 max-rowid = **20** → `18 ≤ 20` ✓  follow child page = **8**
3. Seek to `(8 − 1) × 8192 = 0xE000`. First byte is `0x0d`, so this is a leaf. Read its 5-cell pointer array and walk to the cell whose embedded rowid varint equals 18.

That's **two page reads, no full-table scan**. On a 32-row table that's already useful; on a million-row table the same tree would be at most three levels deep.

The next section actually does the leaf decode that step 3 above hand-waves.

### 7.1 Page 8 — the leaf SQLite landed on (offset `0xE000`)

```
0000e000: 0d00 0000 0502 2900 19fc 1404 0e10 081f  ......).........
0000e010: 0229 ...
```

8-byte leaf header:

| Offset (file)  | Bytes      | Value      | Meaning |
|----------------|------------|------------|---------|
| `0xE000`       | `0d`       | 13         | Leaf table B-tree. |
| `0xE001`–`0xE002` | `00 00` | 0          | No freeblocks. |
| `0xE003`–`0xE004` | `00 05` | **5**      | Five cells (rowids 16–20). |
| `0xE005`–`0xE006` | `02 29` | 0x0229 = 553 | Cell-content area starts 553 bytes into the page (cells grow upward from offset 8 180 down to here). |
| `0xE007`       | `00`       | 0          | Fragmented bytes. |

Cell-pointer array (5 × 2 bytes from `0xE008`):

```
19fc   1404   0e10   081f   0229
```

| Cell | Page-rel offset | File offset | Rowid stored there |
|-----:|-----------------|-------------|--------------------|
| 0    | `0x19FC`        | `0xF9FC`    | 16 — Ramen Shoyu        |
| 1    | `0x1404`        | `0xF404`    | 17 — Okonomiyaki        |
| 2    | `0x0E10`        | `0xEE10`    | **18 — Tempura**        |
| 3    | `0x081F`        | `0xE81F`    | 19 — Pad Thai           |
| 4    | `0x0229`        | `0xE229`    | 20 — Tom Yum Goong      |

### 7.2 Free-space sanity check on page 8

Usable = 8 180. Header (8) + pointer array (10) = 18. Cell-content area occupies `[553, 8180)`, so 7 627 bytes. Free space = `8180 − 18 − 7627 = 535`. `dbstat.unused = 535`. ✓

### 7.3 Decoding cell 2 — the actual "Tempura" row

Bytes at file offset `0xEE10`:

```
0000ee10: 8b71 1206 001b 1d97 4554 656d 7075 7261  .q......ETempura
0000ee20: 4a61 7061 6e65 7365 5374 6570 2d62 792d  JapaneseStep-by-
0000ee30: 7374 6570 2070 7265 7061 7261 7469 6f6e  step preparation
```

Cell layout (leaf table cell) is `<payload-size varint> <rowid varint> <payload>`.

#### 7.3.1 Varint refresher

A SQLite varint is up to nine bytes. Every byte except the ninth uses its **high bit** as "continue", and the low 7 bits as data. To decode:

```
acc = 0
while byte has high bit set:
    acc = (acc << 7) | (byte & 0x7F)
    next byte
acc = (acc << 7) | (byte & 0x7F)        # final byte
```

I'll use that rule twice in the next two paragraphs.

#### 7.3.2 Header bytes of the cell

* `8b 71` → first byte has the continue bit set → `(0x8b & 0x7F) = 0x0b`. Shift, then OR in `0x71`. Result = `(0x0b << 7) | 0x71 = 1408 + 113 = 1521`. **Payload size = 1 521 bytes.**
* `12` → no continue bit → **rowid = 18.** ✓ matches the cell pointer.

#### 7.3.3 The record header

Inside the payload, the first varint is the **record-header length**, followed by one **serial type** varint per column.

| Bytes (file offset) | Decoded         | Column         | What that serial type means |
|---------------------|-----------------|----------------|-----------------------------|
| `06` at `0xEE13`    | header is 6 bytes long | —       | Tells the parser where the body starts. |
| `00` at `0xEE14`    | serial type 0   | `id`           | **NULL** — because `id` is `INTEGER PRIMARY KEY`, the value lives in the rowid varint, not the row body. Saves a few bytes per row. |
| `1b` at `0xEE15`    | serial type 27  | `title`        | TEXT, byte length = `(27 − 13) / 2 = 7` → `"Tempura"`. |
| `1d` at `0xEE16`    | serial type 29  | `cuisine`      | TEXT, byte length = `(29 − 13) / 2 = 8` → `"Japanese"`. |
| `97 45` at `0xEE17` | serial type `(0x17 << 7) \| 0x45 = 3013` | `instructions` | TEXT, byte length = `(3013 − 13) / 2 = 1500` → the 1 500-byte instructions blob. |

Serial-type cheat sheet (the only three classes that show up here):

* `0` → NULL (no body bytes)
* odd ≥ 13 → TEXT, length = `(serial_type − 13) / 2`
* even ≥ 12 → BLOB, length = `(serial_type − 12) / 2`

#### 7.3.4 The body, and the size check that has to hold

After the 6-byte header, the body bytes appear in declared-column order:

* `0xEE19`–`0xEE1F`: `54 65 6D 70 75 72 61` → ASCII **"Tempura"** (7 bytes).
* `0xEE20`–`0xEE27`: `4A 61 70 61 6E 65 73 65` → **"Japanese"** (8 bytes).
* `0xEE28` onward: 1 500 bytes of `"Step-by-step preparation for Tempura (Japanese). Combine the prepared ingredients carefully, …"` (the first 16 bytes of which are visible at `0xEE28` in the dump above).

Total body = `0 + 7 + 8 + 1500 = 1515` bytes. Plus the 6-byte record header that lives **inside** the payload, total payload = `1521` — which matches the varint we read at the very start of the cell. ✓

That single equation (`header_len + Σ column_lengths == payload_varint`) is the cheapest sanity check there is for any leaf cell: if it doesn't hold, your header decode is wrong.

---

## 8. Page 11 — the lopsided tail leaf

Just to look at what happens when a page is **not** full, here's the last leaf (offset `0x14000`):

```
00014000: 0d00 0000 0214 0800 19fe 1408 0000 0000  ................
```

| Field            | Value      |
|------------------|------------|
| Page type        | `0x0d` — leaf table. |
| Cell count       | **2** — only rowids 31 ("Tabbouleh") and 32 ("Shakshuka") live here. |
| Cell-content start | `0x1408` = 5 128 page-relative. The cells sit in the top half of the page; the bottom half is empty. |
| Cell-pointer 0   | `0x19FE` → file `0x159FE` → rowid 31. |
| Cell-pointer 1   | `0x1408` → file `0x15408` → rowid 32. |

Free space = `8180 − 8 − 4 − (8180 − 5128) = 5116` bytes. `dbstat.unused = 5116`. ✓

Five-sixths of this page is unused. That is **normal** after a B-tree split: SQLite doesn't aggressively re-pack pages, because the next insert (rowid 33, 34, …) will land on this page first and use up that space before any further splitting happens.

---

## 9. What this exercise actually taught me

Forcing myself to decode each page out of raw hex changed how I think about a few things:

* **A SQLite database is just an array of equally sized pages.** The schema, the tables, the indexes, even the autoindex that I didn't ask for — they all live inside that array, each addressed by its 1-based page number. There is no separate "table file".
* **Every page is self-describing.** Header tells you what kind of page it is, how many cells it has, and where the cell content starts. The cell-pointer array grows down from the header; the cells grow up from offset `page_size − reserved`. The gap in the middle *is* the free space.
* **Interior pages are routers, not storage.** Each cell is just `(child page, max rowid that lives down there)`, plus a single rightmost pointer for "everything bigger than the last key". On this database, 6 such cells were enough to direct any of the 32 rowids to the correct leaf in a single hop.
* **Leaves are where data lives, and their layout is unforgiving.** Every leaf cell is `payload-size varint` + `rowid varint` + record header + column bytes. If you mis-count even one byte, the next column's serial type will look like garbage. The single equation `header_len + Σ column_lengths == payload_varint` is the discipline that keeps that decode honest.
* **`INTEGER PRIMARY KEY` is free.** In every cell I decoded the `id` column showed up as serial type `0` — its actual value is reconstructed from the rowid varint that already had to be written anyway.
* **`UNIQUE` is not free.** It silently buys you an index page (here page 3, `sqlite_autoindex_cuisines_1`). Worth keeping in mind when you're designing the smallest possible schema.
* **`dbstat` and `.dbinfo` are not magic.** Every number they report — `unused`, `payload`, `ncell`, `pagetype` — can be derived from the bytes on disk in a few subtractions. Doing the derivation by hand a few times is what makes them stop feeling like a black box.
* **`O(log n)` reads is something you can count.** For `WHERE id = 18` we did exactly two page reads (root → leaf). A million-row table at this fanout would do three; a billion-row table, four. That is the entire pitch of a B-tree, and you can watch it happen in 35 hex lines.

---

## 10. Commands I leaned on while writing this

```bash
# Build and dump
sqlite3 recipes.db < seed.sql
xxd recipes.db > hexdump.txt

# Cross-check the topology against my hand decode
sqlite3 recipes.db "PRAGMA page_size;"             # 8192
sqlite3 recipes.db "PRAGMA page_count;"            # 11
sqlite3 recipes.db ".dbinfo"                       # global header values
sqlite3 recipes.db "SELECT name, pageno, pagetype, ncell, payload, unused
                    FROM dbstat ORDER BY pageno;"

# Spot-check a single page in the dump
awk '/^00006000:/,/^00006020:/' hexdump.txt        # page 4 header
awk '/^0000ee10:/,/^0000ee30:/' hexdump.txt        # the 'Tempura' cell
```
