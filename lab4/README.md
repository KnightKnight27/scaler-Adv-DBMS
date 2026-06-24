# Lab 4 – SQLite Hexdump Analysis

## Overview

In this lab we **create a SQLite database** (`characters.db`) from the terminal, then generate a **hexdump** (`exadump.txt`) of the raw binary file. By examining the hex output we can understand the internal structure of a SQLite file — its header, pages, B-tree nodes, and cell data.

---

## Files

| File | Description |
|------|-------------|
| `characters.db` | SQLite database created via terminal commands |
| `exadump.txt` | Hexdump of `characters.db` (generated with `xxd`) |
| `README.md` | This file — analysis of the hexdump |

---

## How the Database Was Created

```bash
# Create the database and table
sqlite3 characters.db "CREATE TABLE characters (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    name    TEXT NOT NULL,
    form    TEXT NOT NULL,
    money   REAL DEFAULT 0,
    history TEXT
);"

# Insert 10 character records
sqlite3 characters.db "INSERT INTO characters (name, form, money, history) VALUES ('Aragorn','Human',1500.00,'Heir of Isildur, ranger of the North, and King of Gondor.');"
sqlite3 characters.db "INSERT INTO characters (name, form, money, history) VALUES ('Gandalf','Wizard',9999.99,'One of the Istari sent to Middle-earth to oppose Sauron.');"
# ... (8 more inserts — see exadump.txt for the full data)

# Generate hexdump
xxd characters.db > exadump.txt
```

---

## What Exists in the First Page (Page 1 — The Header Page)

The first page of every SQLite database is special: it starts with a **100-byte file header** followed by the **schema table** (a B-tree that stores `CREATE TABLE` / `CREATE INDEX` statements).

### SQLite File Header (Bytes 0x00 – 0x63)

Below is the actual hexdump of the first 100 bytes from our `characters.db`:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 000b 0000 0003  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000050: 0000 0000 0000 0000 0000 0000 0000 000b  ................
00000060: 002e 8df8                                ....
```

### Header Field Breakdown

| Offset | Size | Hex Value | Decoded Value | Meaning |
|--------|------|-----------|---------------|---------|
| 0x00 | 16 | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | `SQLite format 3\0` | **Magic string** — identifies this as a SQLite database |
| 0x10 | 2 | `10 00` | 4096 | **Page size** in bytes (0x1000 = 4096) |
| 0x12 | 1 | `01` | 1 | **Write format version** (1 = legacy/rollback journal) |
| 0x13 | 1 | `01` | 1 | **Read format version** (1 = legacy) |
| 0x14 | 1 | `0C` | 12 | **Reserved bytes** per page (used by extensions) |
| 0x15 | 1 | `40` | 64 | **Max embedded payload fraction** (always 64) |
| 0x16 | 1 | `20` | 32 | **Min embedded payload fraction** (always 32) |
| 0x17 | 1 | `20` | 32 | **Leaf payload fraction** (always 32) |
| 0x18 | 4 | `00 00 00 0B` | 11 | **File change counter** — incremented on every transaction commit |
| 0x1C | 4 | `00 00 00 03` | 3 | **Database size** in pages (3 × 4096 = 12,288 bytes) |
| 0x20 | 4 | `00 00 00 00` | 0 | **First freelist trunk page** (0 = no free pages) |
| 0x24 | 4 | `00 00 00 00` | 0 | **Total freelist pages** |
| 0x28 | 4 | `00 00 00 01` | 1 | **Schema cookie** — changes when schema is modified |
| 0x2C | 4 | `00 00 00 04` | 4 | **Schema format number** (4 = current format) |
| 0x30 | 4 | `00 00 00 00` | 0 | **Default page cache size** |
| 0x34 | 4 | `00 00 00 00` | 0 | **Auto-vacuum top-root page** (0 = not auto-vacuum) |
| 0x38 | 4 | `00 00 00 01` | 1 | **Text encoding** (1 = UTF-8) |
| 0x3C | 4 | `00 00 00 00` | 0 | **User version** |
| 0x40 | 4 | `00 00 00 00` | 0 | **Incremental vacuum mode** (0 = disabled) |
| 0x44 | 4 | `00 00 00 00` | 0 | **Application ID** |
| 0x48 | 20 | all zeros | — | **Reserved for expansion** |
| 0x5C | 4 | `00 00 00 0B` | 11 | **Version-valid-for** number |
| 0x60 | 4 | `00 2E 8D F8` | 3051000 | **SQLite version number** that created this DB (3.51.0) |

### Key Observations from the Header

1. **Magic String** — The file always starts with `"SQLite format 3\0"` (16 bytes). This is how any tool can identify a SQLite file.
2. **Page Size = 4096 bytes** — The database is divided into 4 KiB pages. All I/O is done in page-sized chunks.
3. **3 Pages Total** — The file is 12,288 bytes (3 × 4096), confirmed by `ls -l characters.db`.
4. **No Freelist** — No pages have been deleted/reused yet (freelist = 0).
5. **UTF-8 Encoding** — All text (character names, history) is stored as UTF-8.
6. **Rollback Journal Mode** — Write format = 1 (not WAL mode).

---

## Page Layout of the Database

| Page # | Offset Range | Contents |
|--------|-------------|----------|
| **Page 1** | `0x0000 – 0x0FFF` | 100-byte file header + **schema B-tree** (sqlite_master table) |
| **Page 2** | `0x1000 – 0x1FFF` | **characters table B-tree leaf page** — stores all 10 character rows |
| **Page 3** | `0x2000 – 0x2FFF` | **sqlite_sequence table** — tracks AUTOINCREMENT counter |

---

## Analyzing Page 1 — The Schema Table

After the 100-byte header, Page 1 contains a **B-tree leaf page** for the `sqlite_master` table. Starting at offset `0x0064`:

```
00000060:                          0d 00 00 00 02 0e f4 00      ........
00000070: 0f 46 0e f4                                      .F..
```

- `0x0D` → Page type = **13** (leaf table B-tree page)
- `0x0002` → **2 cells** (two schema entries)
- `0x0EF4` → First byte of cell content area (offset 3828)

At offset `0x0EF4` we can read the `CREATE TABLE` statements stored in the schema:

```
00000f40: ...CREATE TABLE sql
00000f50: ite_sequence(nam
00000f60: e,seq)...
```

```
00000f70: ...CREATE TABLE character
00000f80: s (id INTEGER PR
00000f90: IMARY KEY AUTOIN
00000fa0: CREMENT, name TE
00000fb0: XT NOT NULL, for
00000fc0: m TEXT NOT NULL,
00000fd0:  money REAL DEFA
00000fe0: ULT 0, history T
00000ff0: EXT)
```

This confirms the schema is stored **as plain text SQL** inside the database file itself!

---

## Analyzing Page 2 — Character Data in Hex

Page 2 (offset `0x1000`) is the B-tree leaf page holding all 10 character records.

### Page 2 Header (offset 0x1000)

```
00001000: 0d 00 00 00 0a 0c e8 00 0f a5 0f 50 0e fb 0e b0
00001010: 0e 5d 0e 17 0d ca 0d 88 0d 34 0c e8
```

- `0x0D` = Leaf table B-tree page
- `0x000A` = **10 cells** (our 10 character records!)
- Cell pointer array: `0x0FA5, 0x0F50, 0x0EFB, 0x0EB0, ...` (offsets within the page)

### Reading a Character from Raw Hex

At offset `0x1FA5` (Page 2, cell pointer 0x0FA5 → absolute offset 0x1000 + 0x0FA5 = **not quite** — cell pointers are relative to page start):

Absolute offset for first cell = `0x1000 + 0x0FA5 = 0x1FA5`:

```
00001fa0:                 4d 01 06 00 1b 17 02 7f 41 72 61      M.......Ara
00001fb0: 67 6f 72 6e 48 75 6d 61 6e 05 dc 48 65 69 72 20  gornHuman..Heir 
00001fc0: 6f 66 20 49 73 69 6c 64 75 72 2c 20 72 61 6e 67  of Isildur, rang
00001fd0: 65 72 20 6f 66 20 74 68 65 20 4e 6f 72 74 68 2c  er of the North,
00001fe0: 20 61 6e 64 20 4b 69 6e 67 20 6f 66 20 47 6f 6e   and King of Gon
00001ff0: 64 6f 72 2e                                      dor.
```

We can clearly read:
- **Name:** `Aragorn` (ASCII `41 72 61 67 6f 72 6e`)
- **Form:** `Human` (ASCII `48 75 6d 61 6e`)
- **Money:** `05 dc` → 1500 (as integer, since 1500.00 has no fractional part SQLite stores it as integer type 2)
- **History:** `Heir of Isildur, ranger of the North, and King of Gondor.`

### All 10 Characters Visible in the Hexdump

| Offset | Name | Form | Money | History (truncated) |
|--------|------|------|-------|---------------------|
| 0x1FA5 | Aragorn | Human | 1500.00 | Heir of Isildur, ranger of the North... |
| 0x1F50 | Gandalf | Wizard | 9999.99 | One of the Istari sent to Middle-earth... |
| 0x1EFB | Legolas | Elf | 3200.50 | Prince of the Woodland Realm... |
| 0x1EB0 | Gimli | Dwarf | 4800.75 | Son of Gloin, fierce warrior... |
| 0x1E5D | Frodo | Hobbit | 250.00 | Bearer of the One Ring... |
| 0x1E17 | Samwise | Hobbit | 120.30 | Frodos gardener and most faithful friend |
| 0x1DCA | Gollum | Creature | 0.00 | Once a hobbit named Smeagol... |
| 0x1D88 | Eowyn | Human | 2000.00 | Shieldmaiden of Rohan... |
| 0x1D34 | Saruman | Wizard | 8500.00 | Head of the Istari, later corrupted... |
| 0x1CE8 | Boromir | Human | 3000.00 | Son of Denethor, Captain of the White Tower... |

---

## Common SQLite File Format Details

| Property | Value |
|----------|-------|
| **Header size** | 100 bytes (always) |
| **Page sizes** | 512, 1024, 2048, **4096** (default), 8192, 16384, 32768, 65536 |
| **B-tree types** | Interior table (0x05), Leaf table (0x0D), Interior index (0x02), Leaf index (0x0A) |
| **Text encoding** | 1 = UTF-8, 2 = UTF-16le, 3 = UTF-16be |
| **Write modes** | 1 = Rollback journal, 2 = WAL (Write-Ahead Logging) |
| **Type system** | NULL (0), Integer (1-6), Float (7), Text (≥13 odd), Blob (≥12 even) |
| **ACID compliance** | Full — uses rollback journal or WAL for crash recovery |
| **Max DB size** | ~281 TB (2³¹ pages × 65536 bytes/page) |
| **Endianness** | Big-endian for all multi-byte header fields |
| **Concurrency** | Multiple readers, single writer at a time |

---

## How to Reproduce

```bash
# Step 1: Create the database
sqlite3 characters.db "CREATE TABLE characters (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, form TEXT NOT NULL, money REAL DEFAULT 0, history TEXT);"

# Step 2: Insert data
sqlite3 characters.db "INSERT INTO characters (name,form,money,history) VALUES ('Aragorn','Human',1500.00,'Heir of Isildur, ranger of the North, and King of Gondor.');"
# ... repeat for all 10 characters

# Step 3: Generate the hexdump
xxd characters.db > exadump.txt

# Step 4: View database info from header
sqlite3 characters.db ".dbinfo"

# Step 5: Query the data to verify
sqlite3 characters.db ".headers on" ".mode column" "SELECT * FROM characters;"
```

---

## License

This project is part of the **Scaler Advanced DBMS** coursework.
