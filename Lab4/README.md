# Lab 4 — SQLite Database Internal Structure Analysis

- **Role Number:** 24BCS10345
- **Name:** Ansh Mahajan
- **Date:** 2026-06-22

## Aim

Study how SQLite physically lays out data inside a single database file by
building a small database and reading its raw bytes with a hex dump. The goal
is to locate, in the actual file, the structures the engine relies on: the
100-byte file header, the page grid, the B-tree page header, the cell-pointer
array, and the record (cell) encoding — and then trace how a row lookup walks
those structures.

## What I built

A tiny "campus" database, `campus.db`, with two rowid tables:

```sql
CREATE TABLE student (
    roll    INTEGER PRIMARY KEY,   -- becomes the rowid -> no separate key column on disk
    name    TEXT    NOT NULL,
    branch  TEXT    NOT NULL,
    cgpa    REAL
);

CREATE TABLE club (
    club_id INTEGER PRIMARY KEY,
    title   TEXT    NOT NULL,
    mentor  TEXT
);
```

5 students and 3 clubs were inserted (see [build_campus_db.sql](build_campus_db.sql)).
The full build is reproducible:

```bash
rm -f campus.db
sqlite3 campus.db < build_campus_db.sql
xxd campus.db > dump.txt          # raw hex dump analysed below
```

I fixed `PRAGMA page_size = 4096` so the offsets in this report are stable.
The resulting file is **12288 bytes = 3 pages**:

| Page | File offset | Contents |
| --- | --- | --- |
| 1 | `0x0000` | DB header (100 bytes) + `sqlite_schema` B-tree |
| 2 | `0x1000` | `student` table B-tree (root page = 2) |
| 3 | `0x2000` | `club` table B-tree (root page = 3) |

Roots come straight from the catalogue:

```text
sqlite> SELECT name, rootpage, type FROM sqlite_schema;
student|2|table
club|3|table
```

## 1. The 100-byte database header (page 1, offset 0)

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0004 0000 0003  .....@  ........
00000020: 0000 0000 0000 0000 0000 0002 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
...
00000060: 002e 768b                                ..v.
```

Decoding the fields that matter for this lab:

| Offset | Bytes | Field | Value |
| --- | --- | --- | --- |
| 0–15 | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | Magic string | `"SQLite format 3\0"` |
| 16–17 | `10 00` | Page size | `0x1000` = **4096** |
| 18 | `01` | File-format write version | 1 (rollback journal) |
| 19 | `01` | File-format read version | 1 |
| 20 | `00` | Reserved bytes per page | 0 |
| 21–23 | `40 20 20` | Max/min/leaf payload fractions | 64 / 32 / 32 (fixed constants) |
| 24–27 | `00 00 00 04` | File change counter | 4 |
| 28–31 | `00 00 00 03` | **Size of DB in pages** | **3** (matches the file) |
| 32–39 | `00 00 00 00 00 00 00 00` | Freelist trunk page / count | 0 / 0 (no free pages) |
| 40–43 | `00 00 00 02` | Schema cookie | 2 |
| 44–47 | `00 00 00 04` | Schema format number | 4 |
| 56–59 | `00 00 00 01` | Text encoding | **1 = UTF-8** |
| 96–99 | `00 2e 76 8b` | SQLite version number | `0x002e768b` = 3045003 → **3.45.3** |

So the very first 32 bytes already confirm the page size, the page count, and
that nothing is on the freelist — before looking at a single row.

## 2. Page 1 B-tree header (offset 100)

Immediately after the header, page 1 carries the `sqlite_schema` table as a
B-tree. A leaf table B-tree page header is 8 bytes:

```text
00000064: 0d00 0000 020e a700 0f23 0ea7 ....
```

| Offset | Bytes | Meaning |
| --- | --- | --- |
| 100 | `0d` | Page type **0x0D = leaf table B-tree** |
| 101–102 | `00 00` | First freeblock offset = 0 (no internal free space) |
| 103–104 | `00 02` | **Number of cells = 2** (two tables defined) |
| 105–106 | `0e a7` | Cell-content area starts at `0x0ea7` = 3751 |
| 107 | `00` | Fragmented free bytes = 0 |

A *leaf* page has no right-child pointer, so the **cell-pointer array** begins
right at offset 108: `0f 23  0e a7` → two cells at page offsets **3875** and
**3751**. Cells grow *downward* from the end of the page while the pointer
array grows *upward* from the header — the classic slotted-page design.

## 3. A schema record (how a table definition is stored)

The cell at offset `0x0f23` (3875) is the `student` catalogue row:

```text
00000f23: 815a 0107 171b 1b01 830b 7461 626c 6573  ..........tables
00000f33: 7475 6465 6e74 7374 7564 656e 7402 4352  tudentstudent.CR
00000f43: 4541 5445 2054 4142 4c45 ...              EATE TABLE ...
```

A table-leaf cell is `[payload-length varint][rowid varint][record]`:

- `81 5a` → payload length varint = `(1<<7)|0x5a` = **218 bytes**
- `01` → rowid = **1** (first schema entry)
- Record header `07` = 7-byte header, then serial types `17 1b 1b 01 83 0b`:

| Serial type | Decodes to | Column | Value |
| --- | --- | --- | --- |
| `0x17` = 23 | text, len `(23-13)/2` = 5 | `type` | `"table"` |
| `0x1b` = 27 | text, len 7 | `name` | `"student"` |
| `0x1b` = 27 | text, len 7 | `tbl_name` | `"student"` |
| `0x01` = 1 | 8-bit integer | `rootpage` | **2** |
| `0x830b` = 395 | text, len `(395-13)/2` = 191 | `sql` | the `CREATE TABLE` text |

That `rootpage = 2` is exactly how the engine knows to jump to page 2 (offset
`0x1000`) to find `student` rows. The catalogue is itself just an ordinary
B-tree of records.

## 4. A data record, decoded byte-for-byte

Page 2 (`student`) has the same leaf header shape:

```text
00001000: 0d00 0000 050f 6a00 0fc3 0fe1 0fa5 0f88  ......j.........
00001010: 0f6a ...
```

`0x0d`, 0 freeblock, **5 cells**, content area `0x0f6a`, then the 5-entry
cell-pointer array `0fc3 0fe1 0fa5 0f88 0f6a`. Following pointer `0x0fe1`
(page offset 4065 → file offset `0x1fe1`) lands on the row for my own roll
number:

```text
00001fe1: 1cd0 6905 0025 1307 416e 7368 204d 6168  ..i..%..Ansh Mah
00001ff1: 616a 616e 4353 4540 21cc cccc cccc cd     ajanCSE@!......
```

Decoding the cell:

- `1c` → payload length = **28 bytes**
- `d0 69` → rowid varint = `(0x50<<7)|0x69` = **10345**  ← the `roll` value
- Record header `05` = 5 bytes, serial types `00 25 13 07`:

| Serial type | Meaning | Column | Stored value |
| --- | --- | --- | --- |
| `0x00` = 0 | **NULL** | `roll` | *(none — value is the rowid 10345)* |
| `0x25` = 37 | text, len `(37-13)/2` = 12 | `name` | `"Ansh Mahajan"` |
| `0x13` = 19 | text, len 3 | `branch` | `"CSE"` |
| `0x07` = 7 | IEEE-754 8-byte float | `cgpa` | `0x4021cccccccccccd` = **8.9** |

- Body: `41…6e` = `"Ansh Mahajan"`, `43 53 45` = `"CSE"`,
  `40 21 cc cc cc cc cc cd` = the double `8.9`.

Total payload = 5 (header) + 12 + 3 + 8 = **28 bytes**, matching the length
varint exactly.

**Key observation:** the `INTEGER PRIMARY KEY` column is *not* duplicated in
the record — its serial type is `0x00` (NULL) and the real value lives in the
cell's rowid varint. SQLite folds an `INTEGER PRIMARY KEY` onto the rowid, so
it costs zero bytes in the payload. This is why such a column is the cheapest
possible key.

## 5. How a lookup uses these structures

Tracing `SELECT * FROM student WHERE roll = 10345;`:

1. Read the **100-byte header** → page size 4096, so page *N* lives at offset
   `(N-1) * 4096`.
2. Open page 1, read the `sqlite_schema` B-tree, find the `student` row, read
   `rootpage = 2`.
3. Jump to page 2 (offset `0x1000`). Its header says **leaf**, 5 cells.
4. Because `roll` *is* the rowid, do a binary search over the cell-pointer
   array using each cell's rowid varint until 10345 is found (a single page
   here; on a deep table the search would descend interior `0x05` pages first).
5. Parse the record header's serial types and slice the body into
   `(roll, name, branch, cgpa)`.

For a multi-level table the same steps repeat, except step 4 walks **interior**
table pages (type `0x05`, each cell = `[4-byte left-child page][rowid varint]`,
plus a right-most-child pointer in the header) down to the leaf.

## Commands used

```bash
# Build the database deterministically
rm -f campus.db
sqlite3 campus.db < build_campus_db.sql

# Confirm physical facts
sqlite3 campus.db "PRAGMA page_size;"   # 4096
sqlite3 campus.db "PRAGMA page_count;"  # 3
sqlite3 campus.db "PRAGMA encoding;"    # UTF-8
sqlite3 campus.db "SELECT name, rootpage, type FROM sqlite_schema;"

# Raw bytes
xxd campus.db > dump.txt                # full dump committed alongside
xxd -l 100 campus.db                    # just the file header
xxd -s 100 -l 16 campus.db              # page-1 B-tree header
xxd -s 8161 -l 30 campus.db             # the rowid 10345 record
```

## Files

| File | Purpose |
| --- | --- |
| [build_campus_db.sql](build_campus_db.sql) | Reproducible schema + sample data |
| `campus.db` | The 3-page SQLite file analysed above |
| [dump.txt](dump.txt) | Full `xxd` hex dump of `campus.db` |
| `README.md` | This analysis |

## Conclusion

Every logical object maps to a concrete byte range: the file header fixes the
page geometry, each page begins with a B-tree header describing its cell count
and free space, a slotted cell-pointer array indexes the records, and each
record is a self-describing run of varint serial types followed by its payload.
The most memorable finding was that an `INTEGER PRIMARY KEY` is stored as a
`NULL` serial type because its value is the rowid itself — confirming, from the
raw bytes, why that column type carries no storage cost.
