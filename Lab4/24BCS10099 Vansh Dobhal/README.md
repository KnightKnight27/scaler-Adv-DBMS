# Lab 4 - Walking a SQLite Database Byte by Byte

**Name:** Vansh Dobhal  
**Roll Number:** 24BCS10099  
**Course:** Advanced DBMS - Scaler School of Technology

This submission builds and inspects a SQLite database named `recipes.db`. The point of the exercise is to stop treating the database file as a black box: I rebuild it from `seed.sql`, dump the raw bytes into `hexdump.txt`, and then decode the file header, page map, B-tree root, one lookup path, and one complete leaf-cell record.

The data is my own recipe dataset. I used 30 rows in the `recipes` table, and every recipe has a deterministic 950-byte `instructions` field. That makes the table large enough to split across multiple leaf pages while still being small enough to inspect by hand.

---

## Files in This Folder

| File | Purpose |
|------|---------|
| `seed.sql` | Rebuilds the SQLite database from scratch. |
| `recipes.db` | The generated SQLite database file. |
| `hexdump.txt` | `xxd recipes.db` output used for this walkthrough. |
| `README.md` | This byte-level explanation. |

---

## Reproduction Commands

```bash
rm -f recipes.db hexdump.txt
sqlite3 recipes.db < seed.sql
xxd recipes.db > hexdump.txt

sqlite3 recipes.db "PRAGMA page_size;"
sqlite3 recipes.db "PRAGMA page_count;"
sqlite3 recipes.db "SELECT name, pageno, pagetype, ncell, payload, unused
                    FROM dbstat ORDER BY pageno;"
```

The important output values are:

```text
page_size  = 4096
page_count = 12
file size  = 49152 bytes
```

Since `12 * 4096 = 49152`, the page count and actual file size agree exactly.

---

## 1. Page Map

SQLite pages are 1-indexed. With a 4096-byte page size, the file offset of page `p` is:

```text
(p - 1) * 0x1000
```

The database has 12 pages:

| Page | File offset | Object | Type | Cells | Notes |
|-----:|-------------|--------|------|------:|-------|
| 1 | `0x0000` | `sqlite_schema` | leaf | 3 | Schema rows for two tables and one auto-index. |
| 2 | `0x1000` | `cuisines` | leaf | 10 | Small table, all rows fit on one page. |
| 3 | `0x2000` | `sqlite_autoindex_cuisines_1` | index leaf | 10 | Created automatically for `UNIQUE(name)`. |
| 4 | `0x3000` | `recipes` root | interior | 7 | Routes rowids to the recipe leaf pages. |
| 5 | `0x4000` | `recipes` leaf | leaf | 4 | Rowids 1-4. |
| 6 | `0x5000` | `recipes` leaf | leaf | 4 | Rowids 5-8. |
| 7 | `0x6000` | `recipes` leaf | leaf | 4 | Rowids 9-12. |
| 8 | `0x7000` | `recipes` leaf | leaf | 4 | Rowids 13-16. |
| 9 | `0x8000` | `recipes` leaf | leaf | 4 | Rowids 17-20. Decoded below. |
| 10 | `0x9000` | `recipes` leaf | leaf | 4 | Rowids 21-24. |
| 11 | `0xA000` | `recipes` leaf | leaf | 4 | Rowids 25-28. |
| 12 | `0xB000` | `recipes` leaf | leaf | 2 | Rowids 29-30. Tail leaf. |

The shape is therefore a depth-1 table B-tree: page 4 is the interior root, and pages 5 through 12 are leaves.

---

## 2. SQLite File Header

The first 100 bytes are the global SQLite database header. The beginning of `hexdump.txt` is:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0001 0000 000c  .....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000050: 0000 0000 0000 0000 0000 0000 0000 0001  ................
```

Important decoded fields:

| Offset | Bytes | Value | Meaning |
|--------|-------|------:|---------|
| `0x00`-`0x0F` | `53 51 4c 69 ... 33 00` | `SQLite format 3\0` | SQLite magic string. |
| `0x10`-`0x11` | `10 00` | 4096 | Page size. |
| `0x12` | `01` | 1 | Write format version. |
| `0x13` | `01` | 1 | Read format version. |
| `0x14` | `00` | 0 | Reserved bytes per page. Usable page size is also 4096. |
| `0x15` | `40` | 64 | Maximum embedded payload fraction. |
| `0x16` | `20` | 32 | Minimum embedded payload fraction. |
| `0x17` | `20` | 32 | Leaf payload fraction. |
| `0x18`-`0x1B` | `00 00 00 01` | 1 | File change counter. |
| `0x1C`-`0x1F` | `00 00 00 0c` | 12 | Database size in pages. |
| `0x28`-`0x2B` | `00 00 00 02` | 2 | Schema cookie. |
| `0x2C`-`0x2F` | `00 00 00 04` | 4 | Schema format number. |
| `0x38`-`0x3B` | `00 00 00 01` | 1 | Text encoding, UTF-8. |

Two numbers are used throughout the rest of the analysis: page size is 4096 bytes, and reserved bytes per page is 0.

---

## 3. Page 1 - `sqlite_schema`

Page 1 is special because the 100-byte file header comes before its B-tree page header. So the page-1 B-tree header starts at file offset `0x64`, not `0x00`.

```text
00000060: 002e 95c9 0d0f f800 030e 6600 0f3a 0fc7  ..........f..:..
00000070: 0e66 0000 0000 0000 0000 0000 0000 0000  .f..............
```

Starting at `0x64`:

| Field | Bytes | Value | Meaning |
|-------|-------|------:|---------|
| Page type | `0d` | 13 | Leaf table B-tree page. |
| First freeblock | `0f f8` | 4088 | A small freeblock exists near the end of page 1. |
| Cell count | `00 03` | 3 | Schema has three entries. |
| Cell content start | `0e 66` | 3686 | First byte of the cell-content area. |
| Fragmented bytes | `00` | 0 | No fragmented free bytes. |
| Cell pointers | `0f3a`, `0fc7`, `0e66` | - | Three schema cells. |

Those three schema cells describe `cuisines`, `recipes`, and the auto-index SQLite created for the unique cuisine name.

---

## 4. Page 3 - The Hidden Auto-Index

In `seed.sql`, the `cuisines` table has this column definition:

```sql
name TEXT NOT NULL UNIQUE
```

SQLite enforces that uniqueness with an internal index page. Its root page is page 3.

```text
00002000: 0a00 0000 0a0f 8b00 0ff5 0fea 0fdd 0fd1  ................
00002010: 0fc4 0fb9 0fac 0fa0 0f94 0f8b 0000 0000  ................
```

The first byte is `0x0a`, which means index leaf page. The cell count is `0x000a`, or 10 cells, one for each cuisine name. This page is useful because it shows that constraints can create extra B-tree structures even when I did not explicitly write `CREATE INDEX`.

---

## 5. Page 4 - `recipes` Interior Root Page

The `recipes` table has root page 4. Page 4 starts at `(4 - 1) * 4096 = 0x3000`.

```text
00003000: 0500 0000 070f dd00 0000 000c 0ffb 0ff6  ................
00003010: 0ff1 0fec 0fe7 0fe2 0fdd 0000 0000 0000  ................
```

### 5.1 Interior Page Header

| Offset | Bytes | Value | Meaning |
|--------|-------|------:|---------|
| `0x3000` | `05` | 5 | Interior table B-tree page. |
| `0x3001`-`0x3002` | `00 00` | 0 | No freeblocks. |
| `0x3003`-`0x3004` | `00 07` | 7 | Seven routing cells. |
| `0x3005`-`0x3006` | `0f dd` | 4061 | Cell content starts at page-relative offset `0x0FDD`. |
| `0x3007` | `00` | 0 | No fragmented bytes. |
| `0x3008`-`0x300B` | `00 00 00 0c` | 12 | Rightmost child pointer. |

The cell-pointer array starts at `0x300C` and contains:

```text
0ffb 0ff6 0ff1 0fec 0fe7 0fe2 0fdd
```

### 5.2 Interior Cells

An interior table cell is:

```text
4-byte child page number + varint max rowid in that child
```

The bottom of page 4 contains these bytes:

```text
00003fe0: 0b1c 0000 000a 1800 0000 0914 0000 0008  ................
00003ff0: 1000 0000 070c 0000 0006 0800 0000 0504  ................
```

Decoded in pointer order:

| Cell | Page-relative offset | File offset | Bytes | Child page | Max rowid |
|-----:|----------------------|-------------|-------|-----------:|----------:|
| 0 | `0x0FFB` | `0x3FFB` | `00 00 00 05 04` | 5 | 4 |
| 1 | `0x0FF6` | `0x3FF6` | `00 00 00 06 08` | 6 | 8 |
| 2 | `0x0FF1` | `0x3FF1` | `00 00 00 07 0c` | 7 | 12 |
| 3 | `0x0FEC` | `0x3FEC` | `00 00 00 08 10` | 8 | 16 |
| 4 | `0x0FE7` | `0x3FE7` | `00 00 00 09 14` | 9 | 20 |
| 5 | `0x0FE2` | `0x3FE2` | `00 00 00 0a 18` | 10 | 24 |
| 6 | `0x0FDD` | `0x3FDD` | `00 00 00 0b 1c` | 11 | 28 |
| - | Header | `0x3008` | rightmost pointer | 12 | rowids greater than 28 |

The routing rule is therefore:

```text
rowid <=  4  -> page 5
rowid <=  8  -> page 6
rowid <= 12  -> page 7
rowid <= 16  -> page 8
rowid <= 20  -> page 9
rowid <= 24  -> page 10
rowid <= 28  -> page 11
rowid >  28  -> page 12
```

### 5.3 Free-Space Check

Page 4 has:

```text
usable bytes       = 4096
interior header    = 12
cell pointer array = 7 * 2 = 14
cell bodies        = 7 * 5 = 35
unused             = 4096 - 12 - 14 - 35 = 4035
```

`dbstat` also reports `unused = 4035` for page 4, so the arithmetic matches the file.

---

## 6. Lookup Walkthrough: `SELECT * FROM recipes WHERE id = 18`

Rowid 18 is the recipe `Okonomiyaki`.

The lookup path is:

1. The schema says `recipes` has root page 4.
2. Page 4 is an interior page.
3. Compare rowid 18 against the routing keys:
   - `18 > 4`
   - `18 > 8`
   - `18 > 12`
   - `18 > 16`
   - `18 <= 20`, so follow child page 9.
4. Page 9 is a leaf table page.
5. Search page 9's cell pointers and find the cell whose embedded rowid is 18.

This is the B-tree in action: SQLite does not scan all 30 recipe rows. It uses the interior page to jump directly to the correct leaf.

---

## 7. Page 9 - Leaf Page Containing Rowid 18

Page 9 starts at `(9 - 1) * 4096 = 0x8000`.

```text
00008000: 0d00 0000 0400 b300 0c2e 085a 0482 00b3  ...........Z....
00008010: 0000 0000 0000 0000 0000 0000 0000 0000  ................
```

### 7.1 Leaf Header

| Offset | Bytes | Value | Meaning |
|--------|-------|------:|---------|
| `0x8000` | `0d` | 13 | Leaf table B-tree page. |
| `0x8001`-`0x8002` | `00 00` | 0 | No freeblocks. |
| `0x8003`-`0x8004` | `00 04` | 4 | Four cells. |
| `0x8005`-`0x8006` | `00 b3` | 179 | Cell-content area starts at page-relative `0x00B3`. |
| `0x8007` | `00` | 0 | No fragmented bytes. |

The cell pointers are:

```text
0c2e 085a 0482 00b3
```

Decoded:

| Cell | Page-relative offset | File offset | Payload size | Rowid | Recipe |
|-----:|----------------------|-------------|-------------:|------:|--------|
| 0 | `0x0C2E` | `0x8C2E` | 975 | 17 | Miso Soup |
| 1 | `0x085A` | `0x885A` | 977 | 18 | Okonomiyaki |
| 2 | `0x0482` | `0x8482` | 981 | 19 | Kimchi Fried Rice |
| 3 | `0x00B3` | `0x80B3` | 972 | 20 | Bibimbap |

### 7.2 Page 9 Free-Space Check

The cell-content area occupies bytes `[179, 4096)`, so its size is `4096 - 179 = 3917` bytes.

```text
usable bytes       = 4096
leaf header        = 8
cell pointer array = 4 * 2 = 8
cell content       = 3917
unused             = 4096 - 8 - 8 - 3917 = 163
```

`dbstat` reports `unused = 163` for page 9.

---

## 8. Decoding the Rowid 18 Leaf Cell

The rowid 18 cell starts at file offset `0x885A`. The dump around that offset is:

```text
00008850: 7572 652c 2070 7265 702c 8751 1207 0023  ure, prep,.Q...#
00008860: 1d01 8e79 4f6b 6f6e 6f6d 6979 616b 694a  ...yOkonomiyakiJ
00008870: 6170 616e 6573 6524 5265 6369 7065 206e  apanese$Recipe n
00008880: 6f74 6573 2066 6f72 204f 6b6f 6e6f 6d69  otes for Okonomi
```

The cell actually begins at the `87 51` bytes in the first line.

### 8.1 Leaf Cell Header

A table leaf cell starts as:

```text
payload-size varint + rowid varint + record payload
```

For this cell:

| Bytes | Decode | Meaning |
|-------|-------:|---------|
| `87 51` | `(0x07 << 7) + 0x51 = 977` | Payload size is 977 bytes. |
| `12` | 18 | Rowid is 18. |

### 8.2 Record Header

After the rowid comes the record payload. The first byte of the payload is the record-header length.

```text
07 00 23 1d 01 8e 79
```

Decoded:

| Bytes | Value | Column | Meaning |
|-------|------:|--------|---------|
| `07` | 7 | - | Record header is 7 bytes long. |
| `00` | 0 | `id` | NULL in the body because `id` is the rowid. |
| `23` | 35 | `title` | TEXT length `(35 - 13) / 2 = 11`. |
| `1d` | 29 | `cuisine` | TEXT length `(29 - 13) / 2 = 8`. |
| `01` | 1 | `cook_time` | 1-byte signed integer. |
| `8e 79` | 1913 | `instructions` | TEXT length `(1913 - 13) / 2 = 950`. |

### 8.3 Record Body

The record body starts at file offset `0x8864`.

| Column | Bytes | Decoded value |
|--------|------:|---------------|
| `id` | 0 | Stored as rowid 18, not repeated in the body. |
| `title` | 11 | `Okonomiyaki` |
| `cuisine` | 8 | `Japanese` |
| `cook_time` | 1 | `0x24`, decimal 36 |
| `instructions` | 950 | Begins with `Recipe notes for Okonomiyaki...` |

Payload size check:

```text
record header = 7 bytes
body          = 0 + 11 + 8 + 1 + 950 = 970 bytes
payload       = 7 + 970 = 977 bytes
```

That equals the payload-size varint `87 51`, so the decode is internally consistent.

---

## 9. Page 12 - Tail Leaf

The final recipe leaf page is page 12 at offset `0xB000`.

```text
0000b000: 0d00 0000 0208 5900 0c2b 0859 0000 0000  ......Y..+.Y....
```

Decoded:

| Field | Value |
|-------|------:|
| Page type | `0x0d`, leaf table page |
| Cell count | 2 |
| Cell-content start | `0x0859`, decimal 2137 |
| Cell pointer 0 | `0x0C2B`, rowid 29 |
| Cell pointer 1 | `0x0859`, rowid 30 |

The free-space check is:

```text
usable bytes       = 4096
leaf header        = 8
cell pointer array = 2 * 2 = 4
cell content       = 4096 - 2137 = 1959
unused             = 4096 - 8 - 4 - 1959 = 2125
```

`dbstat` reports `unused = 2125`, which matches.

---

## 10. What I Learned

- A SQLite database is one file split into fixed-size pages. In this database each page is exactly 4096 bytes.
- Page 1 stores the schema, but its B-tree header starts after the 100-byte file header.
- A `UNIQUE` constraint creates a hidden index B-tree. Here that is `sqlite_autoindex_cuisines_1` on page 3.
- The `recipes` table is not just a list of rows. It has an interior root page that routes rowids to leaf pages.
- Interior table cells store child page numbers and maximum rowids, while leaf table cells store the actual records.
- `INTEGER PRIMARY KEY` values are stored as rowids, so the `id` column appears as serial type `0` in the record body.
- The most useful correctness check while decoding a leaf cell is: `record header length + body byte lengths = payload size`.

---

## 11. Commands Used for Cross-Checking

```bash
sqlite3 recipes.db "PRAGMA page_size;"
sqlite3 recipes.db "PRAGMA page_count;"
sqlite3 recipes.db "SELECT name, pageno, pagetype, ncell, payload, unused
                    FROM dbstat ORDER BY pageno;"

awk '/^00003000:/,/^00003020:/' hexdump.txt
awk '/^00008000:/,/^00008020:/' hexdump.txt
awk '/^00008850:/,/^00008880:/' hexdump.txt
awk '/^0000b000:/,/^0000b010:/' hexdump.txt
```