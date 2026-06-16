# Lab 4 - SQLite disk pages with `xxd` (IPL mini-db)

This lab builds a tiny IPL database, dumps it in hex, and inspects how SQLite stores table and index B-tree pages. The goal is to connect **page offsets** in a hex dump with **B-tree headers**, **cell pointers**, and **record payloads**.

---

## Files

| File | Purpose |
|------|---------|
| `ipl.db` | SQLite database file |
| `ipl.hex` | Hex dump from `xxd` |
| `create_ipl.sql` | Schema + inserts (defines `ipl_clubs`) |
| `README.md` | This guide |

---

## Build the database

### bash (macOS/Linux)

```bash
cd Lab4/24bcs10005
sqlite3 ipl.db < create_ipl.sql
xxd -g 1 -c 16 ipl.db > ipl.hex
sqlite3 ipl.db ".dbinfo"
sqlite3 ipl.db "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema;"
```

### PowerShell (Windows)

```powershell
cd Lab4/24bcs10005
if (Test-Path ipl.db) { Remove-Item ipl.db }
if (Test-Path ipl.hex) { Remove-Item ipl.hex }
sqlite3 ipl.db < create_ipl.sql
if (Test-Path 'C:\Program Files\Git\usr\bin\xxd.exe') {
	& 'C:\Program Files\Git\usr\bin\xxd.exe' -g 1 -c 16 ipl.db > ipl.hex
} else {
	xxd -g 1 -c 16 ipl.db > ipl.hex
}
sqlite3 ipl.db ".dbinfo"
sqlite3 ipl.db "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema;"
```

Run the commands above to build the DB and inspect the schema; SQLite will report the page size, page count and the `rootpage` numbers for your tables and indexes.

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

At offset `0x64` begins the page-1 B-tree header (for `sqlite_schema`). We should see **2 cells** (one table + one index). Near the end of the page, the SQL text for the created objects will appear, for example:

- `CREATE TABLE ipl_clubs ...`
- `CREATE INDEX idx_ipl_clubs_nrr ...`

From the `sqlite_schema` query, record the rootpages for the table and index.

### 3) Clubs table (`ipl_clubs`)

The rewritten schema stores club records in `ipl_clubs` (club_id, club_name, matches_played, wins, losses, net_run_rate). After building the DB, query `sqlite_schema` to find the table's `rootpage`. Table leaf pages contain the row payloads (club name and stats).

Example logical row (points = wins * 2):

```
(101, 'Chepauk Champions', 14, 9, 5, 0.523)
```

To find a particular club by `club_id`, locate the table leaf page via the cell pointer array and compare rowids.

### 4) Indexes and computed points

This lab's sample schema does not store a `points` column; points can be computed in queries as `wins * 2`. If you add an index later, inspect its `rootpage` from `sqlite_schema` and then locate index-leaf pages in the hex dump.

---

## Optional evidence snippets

Paste short hex lines from our dump to show we located the right pages (examples pulled from `ipl.hex`):

```
00001f10: 01 01 07 44 65 73 65 72 74 20 52 6f 79 61 6c 73  ...Desert Royals
00001f30: 01 01 01 07 43 61 70 69 74 61 6c 73 20 58 49 0e  ....Capitals XI.
00001f50: 01 01 07 53 75 6e 72 69 73 65 20 57 61 72 72 69  ...Sunrise Warri
00001f70: 01 01 01 07 4b 6e 69 67 68 74 20 52 69 64 65 72  ...Knight Rider
00001fa0: 01 01 01 07 42 65 6e 67 61 6c 75 72 75 20 53 74  .Bengaluru Str
00001fc0: 23 01 01 01 07 42 6f 6d 62 61 79 20 42 6c 75 65  #....Bombay Blue
00001fe0: 01 01 01 07 43 68 65 70 61 75 6b 20 43 68 61 6d  ....Chepauk Cham
00001ff0: 70 69 6f 6e 73 0e 09 05 3f e0 bc 6a 7e f9 db 23  pions...?..j~..#
```

---

## Quick checks

```powershell
sqlite3 ipl.db "PRAGMA page_count;"
sqlite3 ipl.db "SELECT * FROM ipl_clubs WHERE wins * 2 = 12;"
sqlite3 ipl.db "EXPLAIN QUERY PLAN SELECT * FROM ipl_clubs WHERE wins * 2 = 12;"
```

Expected plan: computed-expression queries like `wins * 2` are unlikely to use an index unless you create one specifically; expect a table scan unless an index exists.

---