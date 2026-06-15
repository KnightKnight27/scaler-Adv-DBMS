# Lab 4 - SQLite disk pages with `xxd` (IPL mini-db)

**Author:** Nishant Dasgupta
**Branch:** `lab4`
**Tools:** SQLite 3.44.2, `xxd`

This lab builds a tiny IPL database, dumps it in hex, and inspects how SQLite stores table and index B-tree pages. The goal is to connect **page offsets** in a hex dump with **B-tree headers**, **cell pointers**, and **record payloads**.

---

## Files

| File | Purpose |
|------|---------|
| `ipl.db` | SQLite database file |
| `ipl.hex` | Hex dump from `xxd` |
| `create_ipl.sql` | Schema + inserts |
| `README.md` | This guide |

---

## Build the database

### bash (macOS/Linux)

```bash
cd lab4_24BCA10451
sqlite3 ipl.db < create_ipl.sql
xxd -g 1 -c 16 ipl.db > ipl.hex
sqlite3 ipl.db ".dbinfo"
sqlite3 ipl.db "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema;"
```

### PowerShell (Windows)

```powershell
cd lab4_24BCA10451
sqlite3 ipl.db < create_ipl.sql
& "C:\Program Files\Git\usr\bin\xxd.exe" -g 1 -c 16 ipl.db > ipl.hex
sqlite3 ipl.db ".dbinfo"
sqlite3 ipl.db "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema;"
```

Record our outputs here:

```
page size: 4096
page count: 3
```

Schema pointers (from `sqlite_schema`):

| type  | name               | rootpage |
|-------|--------------------|----------|
| table | `teams`            | **2**  |
| index | `idx_teams_points` | **3**  |

---

## How to navigate the hex dump

`xxd` prints 16 bytes per line:

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
```

Rules we will use:

- Page N starts at file offset `(N - 1) * 4096`.
- Page 1 has a 100-byte database header before its B-tree area.
- Cell pointers are 2-byte big-endian offsets from the **start of the page**.

---

## Page anatomy (quick view)

Every B-tree page begins like this:

```
byte 0    : page type
bytes 1-2 : first freeblock (0 if none)
bytes 3-4 : number of cells
bytes 5-6 : start of cell content area
byte 7    : fragmented free bytes
bytes 8.. : cell pointer array (2 * cell count)
```

Page type values we will see here:

- `0x0d` -> table leaf
- `0x0a` -> index leaf

---

## Walkthrough

### 1) Database header (offset 0x0000)

The first 100 bytes describe the file. Confirm:

- SQLite magic string
- page size (4096)
- page count

Paste our header excerpt if needed:

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 0c 40 20 20 00 00 00 03 00 00 00 03  .....@  ........
```

### 2) Schema page (page 1)

At offset `0x64` begins the page-1 B-tree header (for `sqlite_schema`). We should see **2 cells** (one table + one index). Near the end of the page, the SQL text for:

- `CREATE TABLE teams ...`
- `CREATE INDEX idx_teams_points ...`

From the `sqlite_schema` query, record the rootpages for the table and index.

### 3) Teams table (rootpage 2)

Jump to the table rootpage from the catalog. We should see a table-leaf page header with **7 cells**. The payload area contains the team names and points.

Example logical row:

```
(4, 'Kolkata Knight Riders', 12)
```

To locate a row by `id`, we use the cell pointer array and compare rowids.

### 4) Points index (rootpage 3)

Jump to the index rootpage. We should see an index-leaf header with **7 cells**. Each index cell stores:

- key = `points`
- rowid back into the table page

Example lookup:

```
SELECT * FROM teams WHERE points = 12;
```

Find the `(12 -> rowid 4)` entry in the index, then fetch rowid 4 in the table.

---

## Optional evidence snippets

Paste short hex lines from our dump to show we located the right pages:

```
00000060: 00 2e 8d f8 0d 00 00 00 02 0f 25 00 0f 76 0f 25  ..........%..v.%
00001000: 0d 00 00 00 07 0f 41 00 0f da 0f c5 0f a3 0f 87  ......A.........
00002000: 0a 00 00 00 07 0f cb 00 0f cb 0f d1 0f d7 0f dd  ................
00001f40: 00 15 07 04 00 2d 01 52 61 6a 61 73 74 68 61 6e  .....-.Rajasthan
00001f50: 20 52 6f 79 61 6c 73 08 13 06 04 00 29 01 44 65   Royals.....).De
00001f60: 6c 68 69 20 43 61 70 69 74 61 6c 73 09 18 05 04  lhi Capitals....
00001f70: 00 33 01 53 75 6e 72 69 73 65 72 73 20 48 79 64  .3.Sunrisers Hyd
00001f80: 65 72 61 62 61 64 0a 1a 04 04 00 37 01 4b 6f 6c  erabad.....7.Kol
00001f90: 6b 61 74 61 20 4b 6e 69 67 68 74 20 52 69 64 65  kata Knight Ride
00001fa0: 72 73 0c 20 03 04 00 43 01 52 6f 79 61 6c 20 43  rs. ...C.Royal C
00001fb0: 68 61 6c 6c 65 6e 67 65 72 73 20 42 61 6e 67 61  hallengers Banga
00001fc0: 6c 6f 72 65 0e 13 02 04 00 29 01 4d 75 6d 62 61  lore.....).Mumba
00001fd0: 69 20 49 6e 64 69 61 6e 73 10 18 01 04 00 33 01  i Indians.....3.
00001fe0: 43 68 65 6e 6e 61 69 20 53 75 70 65 72 20 4b 69  Chennai Super Ki
00001ff0: 6e 67 73 12 00 00 00 00 00 00 00 00 00 00 00 00  ngs.............
```

---

## Quick checks

```powershell
sqlite3 ipl.db "PRAGMA page_count;"
sqlite3 ipl.db "SELECT * FROM teams WHERE points = 12;"
sqlite3 ipl.db "EXPLAIN QUERY PLAN SELECT * FROM teams WHERE points = 12;"
```

Expected plan: index search on `idx_teams_points`, then rowid lookup in `teams`.

---