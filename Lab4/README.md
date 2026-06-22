# Lab 4 — SQLite on-disk format via `xxd`

**Author:** Dhruv Davda (24BCS10203)
**Branch:** `lab4`
**Tooling:** SQLite 3.50.4 (via Python `sqlite3` module), `xxd` (vim-common)

This lab builds a tiny SQLite database, dumps it byte-by-byte with `xxd`, and walks through the on-disk layout: the database header, B-tree page headers, the cell pointer array, table-leaf cells (rows), and index-leaf cells (key → rowid). Every offset cited below was read directly out of [`campus.hex`](campus.hex).

---

## 1. Files in this folder

| File | Purpose |
|------|---------|
| [`campus.sql`](campus.sql) | DDL + INSERTs used to build the database |
| [`campus.db`](campus.db) | The SQLite database (3 pages × 4096 B = 12,288 B) |
| [`campus.hex`](campus.hex) | Full `xxd -g 1 -c 16 campus.db` dump (768 lines) |
| [`README.md`](README.md) | This walkthrough |

### Reproduce

The default macOS `sqlite3` binary reserves 12 bytes per page for an Apple locking shim, which shifts every cell offset. To get a "clean" page layout I built `campus.db` through Python's bundled SQLite (which uses 0 reserved bytes):

```bash
cd Lab4
rm -f campus.db
python3 -c "import sqlite3; c=sqlite3.connect('campus.db'); c.executescript(open('campus.sql').read())"
xxd -g 1 -c 16 campus.db > campus.hex
sqlite3 campus.db ".dbinfo"
sqlite3 campus.db "SELECT type, name, tbl_name, rootpage FROM sqlite_schema;"
```

Expected:

```
database page size:  4096
database page count: 3
reserved bytes:      0
```

| type  | name                 | rootpage | Role                       |
|-------|----------------------|----------|----------------------------|
| table | `students`           | **2**    | Table B-tree (row storage) |
| index | `idx_students_grade` | **3**    | Index B-tree on `grade`    |

**Page-to-offset rule:** page *N* (1-based) starts at file offset `(N − 1) × 4096`.

| Page | File offset | Contents |
|------|-------------|----------|
| 1    | `0x0000`    | 100-byte DB header + `sqlite_schema` B-tree |
| 2    | `0x1000`    | `students` table B-tree (leaf) |
| 3    | `0x2000`    | `idx_students_grade` index B-tree (leaf) |

---

## 2. Reading `xxd` output

Each line: `FILE_OFFSET: 16 bytes hex  ASCII`.

Example:

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
```

- Left column is the **byte offset in the file** — use it for navigation.
- Jump to page 2: search for `00001000:`. Page 3: `00002000:`.

---

## 3. The 100-byte database header (page 1, bytes 0–99)

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04  ................
```

| Offset | Bytes (hex)      | Field                  | Decoded |
|--------|------------------|------------------------|---------|
| 0–15   | `53 51 4c…20 33 00` | Magic string        | `"SQLite format 3\0"` |
| 16–17  | `10 00`          | Page size              | `0x1000` → **4096** |
| 18     | `01`             | Write format           | 1 (legacy) |
| 19     | `01`             | Read format            | 1 (legacy) |
| 20     | `00`             | Reserved bytes / page  | **0** (full 4096 B usable) |
| 24–27  | `00 00 00 04`    | File change counter    | 4 (one per write + `VACUUM`) |
| 28–31  | `00 00 00 03`    | Database page count    | **3** |
| 40–43  | `00 00 00 03`    | Schema cookie          | 3 |
| 44–47  | `00 00 00 04`    | Schema format          | 4 |
| 56–59  | `00 00 00 01`    | Text encoding          | 1 (UTF-8) |
| 92–95  | `00 00 00 04`    | Version-valid-for      | 4 |

The B-tree portion of page 1 begins **right after** the 100-byte header, at file offset `0x64`.

---

## 4. B-tree page layout (every data page)

From the [SQLite file format spec](https://www.sqlite.org/fileformat.html):

```
┌─────────────────────────────────────────────────────────────┐
│ byte 0      page-type flag                                  │
│ bytes 1–2   first freeblock offset (0 = none)               │
│ bytes 3–4   number of cells                                 │
│ bytes 5–6   start of cell-content area (offset from page)   │
│ byte 7      fragmented free bytes inside cell-content area  │
│ bytes 8..   cell pointer array (2 bytes × cell count)       │
│ ...         unused space                                    │
│ bottom ↑    cell payloads (grow upward)                     │
└─────────────────────────────────────────────────────────────┘
```

Page-type flags relevant to this DB:

| Hex   | Meaning                  |
|-------|--------------------------|
| `0x0d`| Table **leaf** (`PTF_LEAF \| PTF_LEAFDATA \| PTF_INTKEY`) |
| `0x0a`| Index **leaf** (`PTF_LEAF \| PTF_ZERODATA`) |
| `0x05`| Table interior           |
| `0x02`| Index interior           |

A **cell pointer** is a 2-byte big-endian offset *from the start of the page* (not the file) to the cell's first byte.

---

## 5. Page 1 — `sqlite_schema` catalog (rootpage 1)

B-tree header at file offset **`0x64`**:

```
00000060: 00 2e 8a 14 0d 00 00 00 02 0f 2a 00 0f 84 0f 2a  ..........*....*
                       └ B-tree starts here
```

| Field           | Hex      | Meaning |
|-----------------|----------|---------|
| Page type       | `0d`     | Table leaf (the catalog is itself a table) |
| First freeblock | `00 00`  | None |
| Cell count      | `00 02`  | **2 cells** — one row per schema object |
| Cell-content start | `0f 2a` | 3882 (payloads begin at page offset 0x0f2a) |
| Fragmented free | `00`     | None |
| Cell pointers   | `0f 84`, `0f 2a` | Two cells at page offsets 3972 and 3882 |

The cell content lives in the tail of page 1 — readable as ASCII near the bottom:

```
00000f20: ... 58 02 06 17 11 11 01 75 74 61 62 6c 65 73 74  ........utablest
00000f30: 75 64 65 6e 74 73 73 74 75 64 65 6e 74 73 02 43  udentsstudents.C
00000f40: 52 45 41 54 45 20 54 41 42 4c 45 20 73 74 75 64  REATE TABLE stud
...
00000f80: ... 06 17 2f 2f 01 81 01 69 6e 64 65 78 69 64 78  ../...index id x
00000f90: 5f 73 74 75 64 65 6e 74 73 5f 67 72 61 64 65 73  _students_grade s
00000fa0: 74 75 64 65 6e 74 73 03 43 52 45 41 54 45 20 49  tudents.CREATE I
```

Decoded by `SELECT type, name, tbl_name, rootpage FROM sqlite_schema;`:

| type  | name                 | rootpage |
|-------|----------------------|----------|
| table | `students`           | **2**    |
| index | `idx_students_grade` | **3**    |

Those `rootpage` integers (2 and 3) are the **B-tree pointers** that connect the catalog to the rest of the file.

---

## 6. Page 2 — `students` table B-tree (rootpage 2)

Page starts at **`0x1000`**:

```
00001000: 0d 00 00 00 05 0f c4 00 0f f4 0f e9 0f dc 0f d0  ................
00001010: 0f c4 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

| Field              | Value             |
|--------------------|-------------------|
| Page type          | `0d` (table leaf) |
| Cell count         | `00 05` → **5** rows |
| Cell-content start | `0f c4` (page offset 4036) |
| Cell pointers      | `0f f4`, `0f e9`, `0f dc`, `0f d0`, `0f c4` |

These five pointers are sorted by **rowid ascending**, so they map to rowids 1 → 5 even though the cells sit at descending file offsets:

| Cell # | Page offset | File offset | Rowid | Decoded row |
|--------|-------------|-------------|-------|-------------|
| 0      | `0x0ff4`    | `0x1ff4`    | 1     | `(1, 'Aarav',  87)` |
| 1      | `0x0fe9`    | `0x1fe9`    | 2     | `(2, 'Diya',   93)` |
| 2      | `0x0fdc`    | `0x1fdc`    | 3     | `(3, 'Ishaan', 76)` |
| 3      | `0x0fd0`    | `0x1fd0`    | 4     | `(4, 'Meera',  89)` |
| 4      | `0x0fc4`    | `0x1fc4`    | 5     | `(5, 'Rohit',  82)` |

### Decoding one table-leaf cell

A table-leaf cell is laid out as:

```
[ payload size (varint) | rowid (varint) | record-header size (varint) |
  serial-types... | column-values... ]
```

**Cell 0** at file offset `0x1ff4`:

```
00001ff0: 69 79 61 5d 0a 01 04 00 17 01 41 61 72 61 76 57  iya]......AaravW
                  ▲
                  └ cell 0 starts at byte 0x1ff4
```

Bytes `0a 01 04 00 17 01 41 61 72 61 76 57`:

| Byte(s)             | Meaning |
|---------------------|---------|
| `0a`                | Payload size = 10 |
| `01`                | Rowid = 1 |
| `04`                | Record-header size = 4 bytes (3 serial-type bytes + this length byte) |
| `00`                | Serial type 0 → NULL (the `id` column reuses the rowid → stored as NULL) |
| `17`                | Serial type 23 → text of length `(23 − 13) / 2 = 5` |
| `01`                | Serial type 1 → 8-bit signed int |
| `41 61 72 61 76`    | `'Aarav'` (5 bytes) |
| `57`                | `0x57` = **87** (grade) |

Same recipe decodes every other cell.

### Rowid lookup walkthrough

`SELECT * FROM students WHERE id = 3;`

```
QUERY PLAN
`--SEARCH students USING INTEGER PRIMARY KEY (rowid=?)
```

1. Catalog (page 1) says `students.rootpage = 2`.
2. Jump to file offset **`0x1000`**.
3. Read the cell pointer array; binary-search by rowid (table-leaf keys *are* rowids).
4. Pointer for rowid 3 is `0x0fdc` → cell starts at file offset `0x1fdc`.
5. Decode → `(3, 'Ishaan', 76)`.

With only 5 rows everything fits on one leaf, so there are no interior pages.

---

## 7. Page 3 — `idx_students_grade` index B-tree (rootpage 3)

Page starts at **`0x2000`**:

```
00002000: 0a 00 00 00 05 0f e3 00 0f fa 0f f4 0f ef 0f e9  ................
00002010: 0f e3 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
```

| Field              | Value             |
|--------------------|-------------------|
| Page type          | `0a` (index leaf) |
| Cell count         | `00 05` → **5** index entries |
| Cell-content start | `0f e3` (page offset 4067) |
| Cell pointers      | `0f fa`, `0f f4`, `0f ef`, `0f e9`, `0f e3` |

Index-leaf pointers are sorted by **indexed-key ascending** — here that's `grade`:

| Cell # | Page offset | File offset | (grade → rowid) | Student |
|--------|-------------|-------------|-----------------|---------|
| 0      | `0x0ffa`    | `0x2ffa`    | (76 → 3)        | Ishaan  |
| 1      | `0x0ff4`    | `0x2ff4`    | (82 → 5)        | Rohit   |
| 2      | `0x0fef`    | `0x2fef`    | (87 → 1)        | Aarav   |
| 3      | `0x0fe9`    | `0x2fe9`    | (89 → 4)        | Meera   |
| 4      | `0x0fe3`    | `0x2fe3`    | (93 → 2)        | Diya    |

### Decoding one index-leaf cell

```
00002fe0: 00 00 00 05 03 01 01 5d 02 05 03 01 01 59 04 04  .......].....Y..
00002ff0: 03 01 09 57 05 03 01 01 52 05 05 03 01 01 4c 03  ...W....R.....L.
```

**Cell 0** at file offset `0x2ffa` — bytes `05 03 01 01 4c 03`:

| Byte(s) | Meaning |
|---------|---------|
| `05`    | Payload size = 5 |
| `03`    | Record-header size = 3 |
| `01`    | Serial type 1 → i8 (`grade`) |
| `01`    | Serial type 1 → i8 (`rowid`) |
| `4c`    | `grade = 0x4c = 76` |
| `03`    | `rowid = 3` |

**Cell 2** at file offset `0x2fef` is one byte shorter — bytes `04 03 01 09 57`:

| Byte | Meaning |
|------|---------|
| `04` | Payload size = 4 |
| `03` | Record-header size = 3 |
| `01` | Serial type 1 → i8 |
| `09` | Serial type 9 → constant value **1** (no bytes consumed) |
| `57` | `grade = 0x57 = 87` |

So the rowid (1) is encoded entirely as a *serial type* with zero payload bytes — a neat SQLite trick for the constants 0 and 1.

### Index lookup walkthrough

`SELECT * FROM students WHERE grade = 89;`

```
QUERY PLAN
`--SEARCH students USING INDEX idx_students_grade (grade=?)
```

1. Catalog → `idx_students_grade.rootpage = 3`.
2. Jump to file offset **`0x2000`**.
3. Binary-search the 5 index pointers by `grade`. Cell 3 at `0x2fe9` matches: `(89 → 4)`.
4. Follow rowid 4 back to the table: jump to page 2 (`0x1000`), find the cell whose pointer is `0x0fd0`, decode → `(4, 'Meera', 89)`.

Two random reads instead of a full table scan.

---

## 8. B-tree map of `campus.db`

```
                  ┌────────────────────────────────────────┐
                  │ Page 1  @ 0x0000                       │
                  │ [100-byte DB header]                   │
                  │ sqlite_schema (table leaf 0x0d, 2 cells)│
                  │   ├─ students          → rootpage 2    │
                  │   └─ idx_students_grade → rootpage 3   │
                  └─────────────┬──────────────────────────┘
                                │
            ┌───────────────────┴────────────────────┐
            ▼                                        ▼
 ┌──────────────────────┐                ┌────────────────────────────┐
 │ Page 2  @ 0x1000     │                │ Page 3  @ 0x2000           │
 │ students TABLE leaf  │◄─── rowid ─────│ idx_students_grade leaf    │
 │ 0x0d  • 5 row cells  │                │ 0x0a  • 5 (grade,rowid) cells│
 └──────────────────────┘                └────────────────────────────┘
```

| Structure              | Page | Type             | Keys / data |
|------------------------|------|------------------|-------------|
| `sqlite_schema`        | 1    | Table leaf `0x0d`| SQL DDL strings + `rootpage` integers |
| `students`             | 2    | Table leaf `0x0d`| Rowids 1–5 with `(name, grade)` |
| `idx_students_grade`   | 3    | Index leaf `0x0a`| `(grade, rowid)` sorted by grade |

---

## 9. Quick reference — navigating any page

1. **Compute base:** `base = (page_number − 1) × 4096`.
2. **Read page-type flag** at `base` (or `base + 100` for page 1's B-tree section, i.e. `0x64`).
3. **Read cell count** at `base + 3` (2 bytes, big-endian).
4. **Cell pointer array** starts at `base + 8`, length `2 × cell_count`.
5. **Each pointer** `p[i]` ⇒ the cell starts at `base + p[i]`; parse the varint payload-size, varint rowid, then the record header (varints again) and column values per the [serial-type table](https://www.sqlite.org/fileformat2.html#record_format).
6. **Interior nodes** also store a right-child page number after the cell pointer array — not exercised here because every B-tree fits in a single leaf.

---

## 10. Verify with the SQLite CLI

```bash
sqlite3 campus.db "PRAGMA page_count;"          # → 3
sqlite3 campus.db "PRAGMA page_size;"           # → 4096
sqlite3 campus.db "SELECT * FROM students WHERE grade = 89;"
sqlite3 campus.db "EXPLAIN QUERY PLAN SELECT * FROM students WHERE grade = 89;"
sqlite3 campus.db "EXPLAIN QUERY PLAN SELECT * FROM students WHERE id = 3;"
```

---

## References

- [SQLite Database File Format](https://www.sqlite.org/fileformat.html)
- [B-Tree pages](https://www.sqlite.org/fileformat2.html#btree)
- [Record format / serial types](https://www.sqlite.org/fileformat2.html#record_format)
