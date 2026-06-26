# Inspecting a SQLite database with `xxd`

> Lab: build a tiny SQLite database, dump it with `xxd`, and walk through the
> raw bytes — page header, B-tree headers, cell-pointer array, cells, and how a
> rowid lookup is resolved into a file offset.

This folder contains:

| File | Purpose |
|------|---------|
| [xxd-inspect.sh](xxd-inspect.sh) | Bash script: creates `cars.db`, writes hex dumps, prints an annotated walk-through. |
| [cars.db](cars.db) | The SQLite file produced by the script (8192 bytes, 2 pages). |
| [cars.full.hex](cars.full.hex) | `xxd` dump of the entire database. |
| [cars.header.hex](cars.header.hex) | First 100 bytes (the database header). |
| [cars.page1.hex](cars.page1.hex) | Page 1 — the `sqlite_schema` page. |
| [cars.page2.hex](cars.page2.hex) | Page 2 — the data page for `cars`. |

Run it yourself:

```sh
chmod +x xxd-inspect.sh
./xxd-inspect.sh
```

---

## 1. The database we built

```sql
PRAGMA page_size = 4096;
CREATE TABLE cars (
    id    INTEGER PRIMARY KEY,
    make  TEXT,
    model TEXT
);
-- 10 rows: Toyota Corolla, Honda Civic, Ford Mustang, Chevrolet Camaro,
--          Tesla Model 3, BMW M3, Audi A4, Nissan Altima,
--          Hyundai Elantra, Mazda CX-5
```

Result: **2 pages × 4096 bytes = 8192 bytes**.

- **Page 1**: the `sqlite_schema` table (one row — the `CREATE TABLE` itself).
- **Page 2**: the actual `cars` rows (one B-tree leaf page, 10 cells).

Both pages are *table-leaf* B-tree pages (type byte `0x0d`), so the tree is a
single-level tree — every row sits in one leaf. With more rows it would grow
into interior pages (`0x05`), and lookups would walk down through them.

---

## 2. The 100-byte database header

```
file offset
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0002 0000 0002  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
...
```

Field-by-field (all multi-byte fields are **big-endian**):

| Offset | Size | Bytes (from our file) | Meaning |
|-------:|-----:|-----------------------|---------|
| `0x00` | 16 | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | Magic string `"SQLite format 3\0"` |
| `0x10` | 2  | `10 00` | Page size = **0x1000 = 4096** |
| `0x12` | 1  | `01` | File format write version |
| `0x13` | 1  | `01` | File format read version |
| `0x14` | 1  | `00` | Reserved bytes at end of each page |
| `0x15` | 1  | `40` | Max embedded payload fraction (always 64) |
| `0x16` | 1  | `20` | Min embedded payload fraction (always 32) |
| `0x17` | 1  | `20` | Leaf payload fraction (always 32) |
| `0x18` | 4  | `00 00 00 00` | File change counter |
| `0x1C` | 4  | `00 00 00 02` | In-header **database size in pages = 2** |
| `0x20` | 4  | `00 00 00 00` | First freelist trunk page (none) |
| `0x24` | 4  | `00 00 00 00` | Number of freelist pages |
| `0x28` | 4  | `00 00 00 01` | Schema cookie |
| `0x2C` | 4  | `00 00 00 04` | Schema format number (4) |
| `0x60` | 4  | `00 00 00 02` | `user_version` / sqlite version-valid-for |

So just from the header we know: page size 4096, 2 pages, no freelist.

---

## 3. Page 1 — the schema page

Page 1 is special: its B-tree page header doesn't start at offset 0 of the
page, it starts **after the 100-byte database header**, i.e. at file offset
`100 (0x64)`.

### 3.1 Page header (8 bytes, leaf table page)

```
00000064: 0d 00 00 00 01 0f 8a 00  0f 8a 00 00 ...
```

| Page offset | File offset | Bytes | Field |
|------------:|------------:|-------|-------|
| 0 | `0x64` | `0d` | Page type: **leaf table b-tree** |
| 1 | `0x65` | `00 00` | First freeblock offset (0 = none) |
| 3 | `0x67` | `00 01` | **Number of cells on this page = 1** |
| 5 | `0x69` | `0f 8a` | **Cell content area starts at page offset 0x0f8a = 3978** |
| 7 | `0x6B` | `00` | Fragmented free bytes |

(Interior pages would have a 12-byte header — the extra 4 bytes are the
right-child page number. Page 1 is a leaf, so 8 bytes is all we get.)

### 3.2 Cell-pointer array

Immediately after the header. One 2-byte pointer per cell, in **key order**:

```
0000006c: 0f 8a       <- cell #1 -> page offset 0x0f8a (= file offset 0x0f8a)
```

There's exactly one cell, so one pointer.

### 3.3 The cell — sqlite_schema row for `cars`

At file offset `0x0f8a`:

```
00000f80: ........................ 68 01 07 17 15 15
00000f90: 01 81 33 74 61 62 6c 65 63 61 72 73 63 61 72 73
00000fa0: 02 43 52 45 41 54 45 20 54 41 42 4c 45 20 63 61
00000fb0: 72 73 20 28 0a 20 20 20 20 69 64 20 20 20 20 49
00000fc0: 4e 54 45 47 45 52 20 50 52 49 4d 41 52 59 20 4b
00000fd0: 45 59 2c 0a 20 20 20 20 6d 61 6b 65 20 20 54 45
00000fe0: 58 54 2c 0a 20 20 20 20 6d 6f 64 65 6c 20 54 45
00000ff0: 58 54 0a 29
```

A table-leaf cell has this structure:

```
[ payload-size varint ] [ rowid varint ] [ record header + record body ]
```

Breaking it down:

| Page-offset | Bytes | Field | Decoded value |
|------------:|-------|-------|---------------|
| `0x0f8a` | `68` | payload size (varint) | **0x68 = 104 bytes** |
| `0x0f8b` | `01` | rowid (varint) | **1** |
| `0x0f8c` | `07` | record header size | 7 bytes (including this byte) |
| `0x0f8d` | `17` | serial type | 23 → TEXT, `(23-13)/2 = 5` bytes |
| `0x0f8e` | `15` | serial type | 21 → TEXT, 4 bytes |
| `0x0f8f` | `15` | serial type | 21 → TEXT, 4 bytes |
| `0x0f90` | `01` | serial type | 1 → INTEGER, 1 byte |
| `0x0f91` | `81 33` | serial type (varint) | (1<<7)\|0x33 = **179** → TEXT, `(179-13)/2 = 83` bytes |
| `0x0f93` | `74 61 62 6c 65` | column 1 (`type`) | `"table"` |
| `0x0f98` | `63 61 72 73` | column 2 (`name`) | `"cars"` |
| `0x0f9c` | `63 61 72 73` | column 3 (`tbl_name`) | `"cars"` |
| `0x0fa0` | `02` | column 4 (`rootpage`) | **2** ← important! |
| `0x0fa1` | `43 52 45 41 54 45 ...` | column 5 (`sql`) | the 83-byte `CREATE TABLE ...` |

So from page 1 we learned: **the `cars` table's B-tree root is page 2.**

---

## 4. Page 2 — the data page for `cars`

Page 2 starts at file offset **`0x1000` (4096)**.

### 4.1 Page header (8 bytes, leaf table page)

```
00001000: 0d 00 00 00 0a 0f 4d 00
```

| Page offset | File offset | Bytes | Field |
|------------:|------------:|-------|-------|
| 0 | `0x1000` | `0d` | **Leaf table b-tree** |
| 1 | `0x1001` | `00 00` | First freeblock (none) |
| 3 | `0x1003` | `00 0a` | **Number of cells = 10** |
| 5 | `0x1005` | `0f 4d` | Cell content area starts at page offset **`0x0f4d`** (= file offset `0x1f4d`) |
| 7 | `0x1007` | `00` | Fragmented free bytes |

### 4.2 Cell-pointer array (the B-tree "node pointers" of this leaf)

10 cells → 10 × 2 = 20 bytes of pointers, starting at page offset 8
(file offset `0x1008`). Pointers are stored in **key (rowid) ascending order**;
cells themselves are packed from the **end of the page downward**, so the
smallest-rowid pointer points to the highest address.

```
00001000: 0d00 0000 0a0f 4d00 0fe1 0fd1 0fc0 0fab
00001010: 0f99 0f8e 0f82 0f70 0f5c 0f4d
```

| Pointer slot | File offset of slot | Page-relative cell offset | File offset of cell | rowid (decoded) | Row |
|--------------|---------------------|---------------------------|---------------------|-----------------|-----|
|  1 | `0x1008` | `0x0fe1` | `0x1fe1` |  1 | Toyota Corolla |
|  2 | `0x100a` | `0x0fd1` | `0x1fd1` |  2 | Honda Civic |
|  3 | `0x100c` | `0x0fc0` | `0x1fc0` |  3 | Ford Mustang |
|  4 | `0x100e` | `0x0fab` | `0x1fab` |  4 | Chevrolet Camaro |
|  5 | `0x1010` | `0x0f99` | `0x1f99` |  5 | Tesla Model 3 |
|  6 | `0x1012` | `0x0f8e` | `0x1f8e` |  6 | BMW M3 |
|  7 | `0x1014` | `0x0f82` | `0x1f82` |  7 | Audi A4 |
|  8 | `0x1016` | `0x0f70` | `0x1f70` |  8 | Nissan Altima |
|  9 | `0x1018` | `0x0f5c` | `0x1f5c` |  9 | Hyundai Elantra |
| 10 | `0x101a` | `0x0f4d` | `0x1f4d` | 10 | Mazda CX-5 |

This table **is** the B-tree's "node pointer" layout for the leaf. In an
interior page each pointer would be a 4-byte child-page number plus a rowid
key; in a leaf each pointer is a 2-byte byte-offset into the same page.

### 4.3 The actual cells (rows)

Cell contents area (file offsets `0x1f4d`..`0x1ff3`):

```
00001f40: ...........................   0d 0a 04
00001f50: 00 17 15 4d 61 7a 64 61 43 58 2d 35           <- rowid 10  Mazda CX-5
                12 09 04 00
00001f60: 1b 1b 48 79 75 6e 64 61 69 45 6c 61 6e 74 72 a   <- rowid 9  Hyundai Elantra
00001f70: 10 08 04 00 19 19 4e 69 73 73 61 6e 41 6c 74 i   <- rowid 8  Nissan Altima
00001f80: 6d 61
            0a 07 04 00 15 11 41 75 64 69 41 34          <- rowid 7  Audi A4
                                              09 06
00001f90: 04 00 13 11 42 4d 57 4d 33                     <- rowid 6  BMW M3
                              10 05 04 00 17 1b 54
00001fa0: 65 73 6c 61 4d 6f 64 65 6c 20 33               <- rowid 5  Tesla Model 3
                                       13 04 04 00 1f
00001fb0: 19 43 68 65 76 72 6f 6c 65 74 43 61 6d 61 72 o  <- rowid 4  Chevrolet Camaro
00001fc0: 0f 03 04 00 15 1b 46 6f 72 64 4d 75 73 74 61 n  <- rowid 3  Ford Mustang
00001fd0: 67
            0e 02 04 00 17 17 48 6f 6e 64 61 43 69 76 i  <- rowid 2  Honda Civic
00001fe0: 63
            11 01 04 00 19 1b 54 6f 79 6f 74 61 43 6f r  <- rowid 1  Toyota Corolla
00001ff0: 6f 6c 6c 61
```

Take **cell at `0x1fe1`** (rowid = 1) byte by byte:

```
0x1fe1: 11        payload size = 17 bytes
0x1fe2: 01        rowid        = 1
0x1fe3: 04        record-header size = 4 bytes
0x1fe4: 00        col `id`    serial type 0  -> NULL (INTEGER PRIMARY KEY
                                                 is aliased to rowid, so
                                                 the column itself stores NULL)
0x1fe5: 19        col `make`  serial type 25 -> TEXT, (25-13)/2 = 6 bytes
0x1fe6: 1b        col `model` serial type 27 -> TEXT, (27-13)/2 = 7 bytes
0x1fe7..0x1fec:  54 6f 79 6f 74 61      "Toyota"
0x1fed..0x1ff3:  43 6f 72 6f 6c 6c 61   "Corolla"
```

Total cell size on disk = `1 (payload-sz) + 1 (rowid) + 17 (payload) = 19` bytes,
which is exactly the gap between this cell pointer (`0x0fe1`) and the end of
the page (`0x1000 + 0x1000 = 0x2000`, last used byte `0x1ff3`).

---

## 5. How a lookup `SELECT * FROM cars WHERE id = 7` works

The engine resolves this purely by following byte offsets — no scanning of
strings:

1. **Open the file**, read the first 100 bytes.
   - Byte `0x10..0x11` → page size = 4096.
2. **Read page 1** (offset 0). At offset 100 is the page-1 B-tree header.
   - Cell pointer at `0x6C` (`0f 8a`) → cell at `0x0f8a`.
   - Walk the sqlite_schema cell for `name = 'cars'`. Read its `rootpage`
     column → **2**.
3. **Read page 2** = file offset `2 × 4096 = 0x1000`.
   - Page type `0x0d` → leaf, so the row is here (if it exists).
   - Read cell count (`0x000a` = 10) at page offsets 3..4.
4. **Binary-search the cell-pointer array** at page offsets 8..27.
   For each pointer slot `i`, follow it to the cell, decode the rowid
   varint, and compare to 7.
   - Slot 7 (file offset `0x1014`) → page-rel offset `0x0f82` → file offset
     **`0x1f82`**.
5. **Decode the cell at `0x1f82`**:

   ```
   0x1f82: 0a              payload size = 10
   0x1f83: 07              rowid        = 7
   0x1f84: 04              header size  = 4
   0x1f85: 00              id    -> NULL (== rowid)
   0x1f86: 15              make  -> TEXT, (21-13)/2 = 4 bytes
   0x1f87: 11              model -> TEXT, (17-13)/2 = 2 bytes
   0x1f88..0x1f8b: "Audi"
   0x1f8c..0x1f8d: "A4"
   ```

   Done — row returned as `(7, 'Audi', 'A4')`.

The pointer chain for this lookup, written compactly:

```
file[0x10..0x11]  -> page_size = 4096
file[0x1C..0x1F]  -> page_count = 2
file[0x64]        = 0x0d                 page-1 is leaf
file[0x67..0x68]  = 1                    one cell
file[0x6C..0x6D]  -> 0x0f8a              schema cell
file[0x0fa0]      = 0x02                 cars.rootpage = 2
file[0x1000]      = 0x0d                 page-2 is leaf
file[0x1003..04]  = 10                   ten cells
file[0x1014..15]  -> 0x0f82 (page-rel)   slot for rowid 7
file[0x1f82..]    = cell with rowid 7
```

---

## 6. Varints, in one paragraph

SQLite stores most counts and rowids as **varints**: 1–9 bytes, big-endian,
where each byte's high bit (`0x80`) is a "more bytes follow" marker and the
low 7 bits are data. So `0x01` = 1, `0x68` = 104, `0x81 0x33` = `(1 << 7) | 0x33`
= 179. Serial types ≥ 12 are blobs/strings: even = BLOB of `(n-12)/2` bytes,
odd = TEXT of `(n-13)/2` bytes. That's why type `0x17` → 5-byte text and
`0x19` → 6-byte text in the tables above.

---

## 7. B-tree shape of this database

```
                        sqlite_schema (page 1, leaf)
                                  │
                  one cell → tells us cars.rootpage = 2
                                  │
                                  ▼
                          cars root (page 2, leaf)
   ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
   │ ptr→fe1│ ptr→fd1│ ptr→fc0│ ptr→fab│ ptr→f99│ ptr→f8e│ ptr→f82│ ptr→f70│ ptr→f5c│ ptr→f4d│
   └────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘
       rowid=1   2        3        4        5        6        7        8        9        10
     Toyota    Honda    Ford    Chev.   Tesla     BMW     Audi    Nissan  Hyundai  Mazda
     Corolla   Civic   Mustang  Camaro  Model 3   M3       A4     Altima  Elantra   CX-5
```

With more rows the tree would grow another level: page 2 would become an
*interior* page (type `0x05`) holding 4-byte child-page numbers + rowid keys,
and the cells would migrate down to new leaf pages. The lookup algorithm in
§5 would gain one step — at the interior page, binary-search the keys, follow
the matching child pointer — but it's the same pattern: header → pointer
array → key compare → next offset.
