# Lab 4 - SQLite on-disk format with `xxd`

**Author:** Pratham Jain  
**Branch:** `lab4`   
**Tooling:** SQLite 3.44.2, `xxd` (Git for Windows)

This lab creates a real SQLite database, dumps it with `xxd`, and walks through **page layout**, **B-tree nodes**, **cell pointers**, and **lookups** using file offsets from the hex dump.

**AI is used to make this documentation better LOL. All other things were done by me! :)**

---

## 1. Artefacts in this folder

| File | Purpose |
|------|---------|
| `campus.db` | Live SQLite database (12,288 bytes = 3 × 4096-byte pages) |
| `campus.hex` | Full `xxd -g 1 -c 16 campus.db` dump (attach / submit with report) |
| `create_campus.sql` | SQL used to build the database |
| `README.md` | This walkthrough |

### Reproduce

```powershell
cd lab4
sqlite3 campus.db < create_campus.sql
& "C:\Program Files\Git\usr\bin\xxd.exe" -g 1 -c 16 campus.db > campus.hex
sqlite3 campus.db ".dbinfo"
sqlite3 campus.db "SELECT type, name, tbl_name, rootpage, sql FROM sqlite_schema;"
```

Expected metadata:

```
database page size:  4096
database page count: 3
```

Schema catalog:

| type  | name                 | rootpage | Role                          |
|-------|----------------------|----------|-------------------------------|
| table | `students`           | **2**    | Table B-tree (row storage)    |
| index | `idx_students_grade` | **3**    | Index B-tree on `grade`       |

**Addressing rule:** page *N* (1-based) starts at file offset `(N - 1) × page_size`.

| Page | File offset | Size   | Contents                                      |
|------|-------------|--------|-----------------------------------------------|
| 1    | `0x0000`    | 4096 B | 100-byte DB header + `sqlite_schema` B-tree   |
| 2    | `0x1000`    | 4096 B | `students` table B-tree (leaf)              |
| 3    | `0x2000`    | 4096 B | `idx_students_grade` index B-tree (leaf)      |

---

## 2. Reading `xxd` output

Each line: `FILE_OFFSET: 16 bytes hex  ASCII`

Example (database magic):

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
```

- Left column = **byte offset in the file** (use this for navigation).
- To jump to page 2: search for `00001000:`.
- To jump to page 3: search for `00002000:`.

---

## 3. Database header (page 1, bytes 0–99)

The first 100 bytes are **not** a normal B-tree page header; they describe the whole file.

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 03  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 03 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04  ................
```

| Offset | Bytes (hex) | Field | Value in `campus.db` |
|--------|-------------|-------|----------------------|
| 0      | `53 51 4c…` | Magic | `"SQLite format 3\0"` |
| 16–17  | `10 00` | Page size | `0x1000` → **4096** |
| 18     | `01` | Write format | 1 |
| 19     | `01` | Read format | 1 |
| 24–27  | `00 00 00 04` | File change counter | 4 |
| 28–31  | `00 00 00 03` | **Database page count** | **3** |
| 40–43  | `00 00 00 03` | Schema cookie | 3 |
| 44–47  | `00 00 00 04` | Schema format | 4 |
| 68–71  | `00 00 00 01` | Text encoding | 1 (UTF-8) |

At offset **0x64** (byte 100) the **B-tree part of page 1** begins (catalog for `sqlite_schema`).

---

## 4. B-tree page layout (all data pages)

Every B-tree page shares this structure (see [SQLite file format](https://www.sqlite.org/fileformat.html)):

```
┌─────────────────────────────────────────────────────────────┐
│  byte 0      : page type flags                              │
│  bytes 1–2   : first freeblock offset (0 = none)            │
│  bytes 3–4   : number of cells                              │
│  bytes 5–6   : start of cell content area (from page top)   │
│  byte 7      : fragmented free bytes in cell content area   │
│  bytes 8..   : cell pointer array (2 bytes × cell count)    │
│  ... unused ...                                             │
│  bottom ↑    : cell payloads (grow upward)                  │
└─────────────────────────────────────────────────────────────┘
```

**Page type flags** (combined flag byte):

| Hex | Meaning |
|-----|---------|
| `0x0d` | Table **leaf** (`PTF_LEAF \| PTF_LEAFDATA \| PTF_INTKEY`) |
| `0x0a` | Index **leaf** (`PTF_LEAF \| PTF_ZERODATA`) |
| `0x05` | Table interior |
| `0x02` | Index interior |

**Cell pointer:** 2-byte big-endian offset from **start of page** to the first byte of that cell’s payload.

**Lookup recipe:**

1. Read page type + cell count at top of page.
2. Read cell pointer array.
3. For each pointer `P`, go to offset `P` in the page and parse the cell (varint header + fields).

---

## 5. Page 1 — `sqlite_schema` B-tree (catalog)

B-tree header at file offset **0x64**:

```
00000060: 00 2e 72 a2 0d 00 00 00 02 0f 2a 00 0f 84 0f 2a  ..r.......*....*
          └─ end of 100-byte DB header ─┘└ B-tree starts here
```

| Field | Hex | Meaning |
|-------|-----|---------|
| Type | `0d` | Table leaf (catalog stores schema rows as table records) |
| Freeblock | `00 00` | None |
| Cell count | `00 02` | **2 cells** (one table + one index) |
| Cell content start | `0f 2a` | Payloads begin at page offset 3882 |
| Cell pointers | `0f 2a`, `0f 84` | Two cells at offsets 3882 and 3972 |

### Cell payloads (bottom of page 1)

Schema rows are visible as ASCII near the end of page 1:

```
00000f20: ... 58 02 06 17 ... indexidx_students_grade ...
00000f50: ... CREATE INDEX idx_students_grade ON students(grade)
00000f80: ... 7a 01 07 17 ... tablestudents ...
00000fa0: ... CREATE TABLE students ( id INTEGER PRIMARY KEY, ...
```

Decoded from `sqlite_schema` query:

- **Cell 1:** `CREATE TABLE students` → **rootpage = 2**
- **Cell 2:** `CREATE INDEX idx_students_grade` → **rootpage = 3**

Those rootpage numbers are the **B-tree pointers** from the catalog into pages 2 and 3. There are no child page pointers on interior nodes here because each structure fits in a single leaf page.

---

## 6. Page 2 — `students` table B-tree (rootpage 2)

Page starts at **`0x1000`**:

```
00001000: 0d 00 00 00 05 0f c9 00 0f f4 0f ea 0f de 0f d3  ................
00001fc0: ... 00 08 05 04 00 13 01 45 76 65 ...
00001fd0: ... 44 61 76 65 ...
00001ff0: ... 42 6f 62 ... 41 6c 69 63 65 ...
```

| Field | Value |
|-------|-------|
| Type `0d` | Table leaf |
| Cell count | **5** (five rows) |
| Cell pointers | `0x0fc9`, `0x0ff4`, `0x0fea`, `0x0fde`, `0x0fd3` |

Cells are stored **near the bottom** of the page (high offsets). Reading upward from `0x1fc0` you see row payloads with embedded text:

- `Alice`, `Bob`, `Carol`, `Dave`, `Eve` and integer grades (92, 85, 91, 78, 88).

### Table leaf cell format (simplified)

Each row cell:

1. **Payload size** (varint)
2. **Rowid** (varint) — for `INTEGER PRIMARY KEY`, matches `id`
3. **Record header** (column types)
4. **Column values** (serial types: integers, strings, etc.)

Example logical row (not raw hex): `(1, 'Alice', 92)`.

### Lookup on the table (rowid / PK)

To fetch `id = 3`:

1. Open **page 2** (`0x1000`).
2. Binary-search the **cell pointer array** by rowid (table leaf keys are rowids).
3. Follow pointer to cell at ~`0x0fd3` region; decode record → `(3, 'Carol', 91)`.

With only five rows, everything sits on one leaf page — no interior nodes.

---

## 7. Page 3 — `idx_students_grade` index B-tree (rootpage 3)

Page starts at **`0x2000`**:

```
00002000: 0a 00 00 00 05 0f e3 00 0f fa 0f f4 0f ee 0f e8  ................
00002fe0: 00 00 00 04 03 01 09 5c 05 03 01 01 5b 03 05 03  .......\....[...
00002ff0: 01 01 58 05 05 03 01 01 55 02 05 03 01 01 4e 04  ..X.....U.....N.
```

| Field | Value |
|-------|-------|
| Type `0a` | Index leaf |
| Cell count | **5** |
| Cell pointers | `0x0fe3`, `0x0ffa`, `0x0ff4`, `0x0fee`, `0x0fe8` |

Index leaf cells store:

1. **Key** — indexed column (`grade`), ascending order in the dump
2. **Rowid** — pointer back to the table row on page 2

Trailing bytes (e.g. `03 01 09` …) are varint-encoded **(grade, rowid)** pairs: grade 78→rowid 4, 85→2, 88→5, 91→3, 92→1 when read in sort order.

### Index lookup walkthrough

**Query:** `SELECT * FROM students WHERE grade = 91;`

1. Catalog (page 1) says index `idx_students_grade` has **rootpage 3**.
2. Go to file offset **`0x2000`** (page 3).
3. On this index leaf, binary-search keys for grade **91**.
4. Find index entry **(91 → rowid 3)**.
5. Jump to **page 2**, find table cell with rowid **3** → `('Carol', 91)`.

**No interior pages** — rootpage points directly to a leaf because the index is tiny.

---

## 8. B-tree map for `campus.db`

```
                    ┌──────────────────────────────────────┐
                    │  Page 1  @ 0x0000                    │
                    │  [100-byte file header]              │
                    │  sqlite_schema (table leaf, 2 cells) │
                    │    ├─ rootpage 2 → students table    │
                    │    └─ rootpage 3 → idx_students_grade│
                    └──────────────┬───────────────────────┘
                                   │
              ┌────────────────────┴────────────────────┐
              ▼                                         ▼
   ┌──────────────────────┐              ┌──────────────────────────┐
   │  Page 2  @ 0x1000    │              │  Page 3  @ 0x2000        │
   │  students TABLE leaf │◄─ rowid ─────│  idx_students_grade      │
   │  5 row cells         │              │  INDEX leaf (grade→rowid)│
   └──────────────────────┘              └──────────────────────────┘
```

| Structure | Page | Type | Child pointers | Keys / data |
|-----------|------|------|----------------|-------------|
| `sqlite_schema` | 1 | Table leaf `0x0d` | rootpage 2, 3 in cell bodies | SQL DDL strings |
| `students` | 2 | Table leaf `0x0d` | — | Rowids 1–5, column values |
| `idx_students_grade` | 3 | Index leaf `0x0a` | rowid → page 2 | Grades 78…92 |

---

## 9. Annotated hex excerpts (from real dump)

### 9.1 File header + start of page-1 B-tree

```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 03  page_size=4096, pages=3
00000060: 00 2e 72 a2 0d 00 00 00 02 0f 2a 00 0f 84 0f 2a  schema btree: 2 cells
```

### 9.2 Page 2 — table rows (tail of page)

```
00001000: 0d 00 00 00 05 0f c9 00 0f f4 0f ea 0f de 0f d3  table leaf, 5 cells
00001ff0: 42 6f 62 55 0a 01 04 00 17 01 41 6c 69 63 65 5c  ... Bob ... Alice
```

### 9.3 Page 3 — index entries (tail of page)

```
00002000: 0a 00 00 00 05 0f e3 00 0f fa 0f f4 0f ee 0f e8  index leaf, 5 cells
00002fe0: 00 00 00 04 03 01 09 5c 05 03 01 01 5b ... 4e 04  (grade,rowid) payloads
```

The **full** dump is in [`campus.hex`](campus.hex) (768 lines). Search offsets above to verify every pointer.

---

## 10. Quick reference — navigating any page

1. **Compute base:** `base = (page_number - 1) * 4096`.
2. **Read type** at `base` (or `base + 100` only for page 1’s B-tree section at `base + 0x64`).
3. **Read cell count** at `base + 3` (2 bytes, big-endian).
4. **Cell pointers** start at `base + 8`, length `2 × cell_count`.
5. **Each pointer** `ptr[i]` → cell starts at `base + ptr[i]`; parse varints per SQLite record format.
6. **Interior nodes** also store a **right-child page number** after the cell pointer array; not needed for this tiny DB.

---

## 11. Verify with SQLite CLI

```powershell
sqlite3 campus.db "PRAGMA page_count;"
sqlite3 campus.db "SELECT * FROM students WHERE grade = 91;"
sqlite3 campus.db "EXPLAIN QUERY PLAN SELECT * FROM students WHERE grade = 91;"
```

Expected plan: search `idx_students_grade` then lookup on `students`.

---

## References

- [SQLite Database File Format](https://www.sqlite.org/fileformat.html)
- [B-Tree pages](https://www.sqlite.org/fileformat2.html#btree)
- [Record format / serial types](https://www.sqlite.org/fileformat2.html#record_format)
