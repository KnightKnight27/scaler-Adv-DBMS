# Lab 4: SQLite Hex Dump Exploration

## 1. Purpose of the Lab

The earlier labs gradually moved from normal file handling to database internals:

- The file read/write lab showed that persistent data finally becomes bytes in a file.
- The SQLite vs PostgreSQL lab showed why SQLite is easy to inspect: the whole database is a single file.
- The Clock Sweep lab showed how a DBMS manages pages in memory.
- This lab connects those ideas by opening a real SQLite database as raw bytes and reading the page and B-tree layout directly.

In this experiment I created a small SQLite database, inserted real rows, dumped the first two pages with `xxd`, and manually followed the bytes for the database header, schema page, table page, cell pointers, and record payloads.

## 2. Files Submitted

The work is in this folder:

```text
Lab4/10044-Harshita-Hirawat/
```

Files generated:

```text
lab4_sqlite_hex.db       real SQLite database file
page1_xxd.txt            full xxd dump of page 1
page2_xxd.txt            full xxd dump of page 2
page1_first_512_xxd.txt  shorter page 1 dump for quick reading
page2_first_512_xxd.txt  shorter page 2 dump for quick reading
README.md                this explanation
```

The database file size is 8192 bytes. Since the page size is 4096 bytes, the file contains exactly two pages.

## 3. Database Created

I created one table named `students` and inserted five rows.

```sql
PRAGMA page_size = 4096;

CREATE TABLE students(
    id INTEGER PRIMARY KEY,
    roll_no TEXT NOT NULL,
    name TEXT NOT NULL,
    topic TEXT NOT NULL,
    score INTEGER NOT NULL
);

INSERT INTO students(roll_no, name, topic, score) VALUES
('10044', 'Harshita Hirawat', 'SQLite hex navigation', 91),
('10045', 'Aarav Mehta', 'B-tree cells', 84),
('10046', 'Nisha Rao', 'Page headers', 88),
('10047', 'Kabir Shah', 'Record format', 79),
('10048', 'Meera Iyer', 'Varints and payloads', 95);
```

The page size was confirmed using:

```sql
PRAGMA page_size;
```

Output:

```text
4096
```

The table root page was checked from `sqlite_schema`:

```sql
SELECT name, rootpage, sql
FROM sqlite_schema
WHERE type = 'table';
```

Output:

```text
students|2|CREATE TABLE students(id INTEGER PRIMARY KEY, roll_no TEXT NOT NULL, name TEXT NOT NULL, topic TEXT NOT NULL, score INTEGER NOT NULL)
```

This means:

- Page 1 stores the SQLite database header and the schema table.
- Page 2 is the root B-tree page for my `students` table.

## 4. Commands Used for Hex Dump

On this machine, `xxd` was available through Git for Windows:

```powershell
& 'C:\Program Files\Git\usr\bin\xxd.exe' -g 1 -l 4096 lab4_sqlite_hex.db > page1_xxd.txt
& 'C:\Program Files\Git\usr\bin\xxd.exe' -g 1 -s 4096 -l 4096 lab4_sqlite_hex.db > page2_xxd.txt
```

Meaning of the options:

- `-g 1` prints bytes one by one instead of grouping them.
- `-l 4096` reads one SQLite page.
- `-s 4096` skips the first page and starts from page 2.

## 5. Page 1: SQLite Database Header

The first 100 bytes of page 1 are the database file header. The dump begins like this:

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00  ................
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 02  ................
```

Important fields:

| File offset | Bytes | Meaning |
|---|---:|---|
| `0x00` to `0x0f` | `53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00` | Magic string: `SQLite format 3\0` |
| `0x10` to `0x11` | `10 00` | Page size. `0x1000` = 4096 bytes |
| `0x12` | `01` | File format write version |
| `0x13` | `01` | File format read version |
| `0x14` | `00` | Reserved bytes per page |
| `0x15` | `40` | Maximum embedded payload fraction |
| `0x16` | `20` | Minimum embedded payload fraction |
| `0x17` | `20` | Leaf payload fraction |
| `0x1c` to `0x1f` | `00 00 00 02` | Database size in pages = 2 |
| `0x5c` to `0x5f` | `00 00 00 02` | SQLite version-valid-for number in this file |
| `0x60` to `0x63` | `00 2e 95 c9` | SQLite library version number stored in the file |

The most important field for this lab is the page size:

```text
00000010: 10 00
```

SQLite stores multi-byte header values in big-endian order. Therefore `10 00` means `0x1000`, which is decimal 4096.

## 6. Page 1: Schema B-tree Page

On page 1, the first 100 bytes are the database header. Immediately after that, at file offset `0x64`, the B-tree page header starts.

```text
00000060: 00 2e 95 c9 0d 00 00 00 01 0f 5b 00 0f 5b 00 00  ..........[..[..
```

Breaking the B-tree header from offset `0x64`:

| File offset | Bytes | Meaning |
|---|---:|---|
| `0x64` | `0d` | Page type = table leaf page |
| `0x65` to `0x66` | `00 00` | First freeblock offset = none |
| `0x67` to `0x68` | `00 01` | Number of cells = 1 |
| `0x69` to `0x6a` | `0f 5b` | Start of cell content area = `0x0f5b` |
| `0x6b` | `00` | Fragmented free bytes = 0 |
| `0x6c` to `0x6d` | `0f 5b` | Cell pointer 1 = `0x0f5b` |

The schema cell starts at offset `0x0f5b` inside page 1. In this database page 1 also begins at file offset `0x0000`, so the page offset and file offset are the same for page 1.

Schema text found at the end of page 1:

```text
00000f50: 00 00 00 00 00 00 00 00 00 00 00 81 22 01 07 17  ............"...
00000f60: 1d 1d 01 82 17 74 61 62 6c 65 73 74 75 64 65 6e  .....tablestuden
00000f70: 74 73 73 74 75 64 65 6e 74 73 02 43 52 45 41 54  tsstudents.CREAT
00000f80: 45 20 54 41 42 4c 45 20 73 74 75 64 65 6e 74 73  E TABLE students
00000f90: 28 69 64 20 49 4e 54 45 47 45 52 20 50 52 49 4d  (id INTEGER PRIM
00000fa0: 41 52 59 20 4b 45 59 2c 20 72 6f 6c 6c 5f 6e 6f  ARY KEY, roll_no
00000fb0: 20 54 45 58 54 20 4e 4f 54 20 4e 55 4c 4c 2c 20   TEXT NOT NULL,
00000fc0: 6e 61 6d 65 20 54 45 58 54 20 4e 4f 54 20 4e 55  name TEXT NOT NU
00000fd0: 4c 4c 2c 20 74 6f 70 69 63 20 54 45 58 54 20 4e  LL, topic TEXT N
00000fe0: 4f 54 20 4e 55 4c 4c 2c 20 73 63 6f 72 65 20 49  OT NULL, score I
00000ff0: 4e 54 45 47 45 52 20 4e 4f 54 20 4e 55 4c 4c 29  NTEGER NOT NULL)
```

This cell is a row from `sqlite_schema`. It stores:

```text
type     = table
name     = students
tbl_name = students
rootpage = 2
sql      = CREATE TABLE students(...)
```

The value `rootpage = 2` is the link from the schema to the actual table B-tree.

## 7. Page 2: Table B-tree Header

Page 2 starts at file offset:

```text
page 2 offset = (2 - 1) * 4096 = 4096 = 0x1000
```

The first bytes of page 2 are:

```text
00001000: 0d 00 00 00 05 0f 34 00 0f cd 0f a8 0f 85 0f 60  ......4........`
00001010: 0f 34 00 00 00 00 00 00 00 00 00 00 00 00 00 00  .4..............
```

Breaking the page 2 B-tree header:

| File offset | Page offset | Bytes | Meaning |
|---|---:|---:|---|
| `0x1000` | `0x0000` | `0d` | Page type = table leaf page |
| `0x1001` to `0x1002` | `0x0001` | `00 00` | First freeblock offset = none |
| `0x1003` to `0x1004` | `0x0003` | `00 05` | Number of cells = 5 |
| `0x1005` to `0x1006` | `0x0005` | `0f 34` | Start of cell content area = `0x0f34` |
| `0x1007` | `0x0007` | `00` | Fragmented free bytes = 0 |

After the 8-byte header comes the cell pointer array. Each pointer is a 2-byte offset measured from the start of the page, not from the start of the file.

| Pointer position | Bytes | Page offset | File offset | Row |
|---|---:|---:|---:|---:|
| 1 | `0f cd` | `0x0fcd` | `0x1fcd` | rowid 1 |
| 2 | `0f a8` | `0x0fa8` | `0x1fa8` | rowid 2 |
| 3 | `0f 85` | `0x0f85` | `0x1f85` | rowid 3 |
| 4 | `0f 60` | `0x0f60` | `0x1f60` | rowid 4 |
| 5 | `0f 34` | `0x0f34` | `0x1f34` | rowid 5 |

The pointers are in key order, so they point to rowid 1, 2, 3, 4, and 5. The actual cell content is packed from the end of the page backward, which is why the physical addresses go downward as more rows are inserted.

## 8. Page 2: Actual Record Bytes

The end of page 2 contains the five row cells:

```text
00001f30: 00 00 00 00 2a 05 06 00 17 21 35 01 31 30 30 34  ....*....!5.1004
00001f40: 38 4d 65 65 72 61 20 49 79 65 72 56 61 72 69 6e  8Meera IyerVarin
00001f50: 74 73 20 61 6e 64 20 70 61 79 6c 6f 61 64 73 5f  ts and payloads_
00001f60: 23 04 06 00 17 21 27 01 31 30 30 34 37 4b 61 62  #....!'.10047Kab
00001f70: 69 72 20 53 68 61 68 52 65 63 6f 72 64 20 66 6f  ir ShahRecord fo
00001f80: 72 6d 61 74 4f 21 03 06 00 17 1f 25 01 31 30 30  rmatO!.....%.100
00001f90: 34 36 4e 69 73 68 61 20 52 61 6f 50 61 67 65 20  46Nisha RaoPage
00001fa0: 68 65 61 64 65 72 73 58 23 02 06 00 17 23 25 01  headersX#....#%.
00001fb0: 31 30 30 34 35 41 61 72 61 76 20 4d 65 68 74 61  10045Aarav Mehta
00001fc0: 42 2d 74 72 65 65 20 63 65 6c 6c 73 54 31 01 06  B-tree cellsT1..
00001fd0: 00 17 2d 37 01 31 30 30 34 34 48 61 72 73 68 69  ..-7.10044Harshi
00001fe0: 74 61 20 48 69 72 61 77 61 74 53 51 4c 69 74 65  ta HirawatSQLite
00001ff0: 20 68 65 78 20 6e 61 76 69 67 61 74 69 6f 6e 5b   hex navigation[
```

### Decoding Row 1

The first cell pointer says row 1 starts at page offset `0x0fcd`, which is file offset `0x1fcd`.

Bytes from there:

```text
31 01 06 00 17 2d 37 01 31 30 30 34 34 48 61 72 73 68 69 ...
```

Cell interpretation:

| Part | Bytes | Meaning |
|---|---:|---|
| Payload size | `31` | `0x31` = 49 bytes |
| Rowid key | `01` | rowid = 1 |
| Record header size | `06` | header is 6 bytes |
| Serial type 1 | `00` | `id` is stored as NULL in payload because `INTEGER PRIMARY KEY` is the rowid |
| Serial type 2 | `17` | text length `(0x17 - 13) / 2 = 5`, value `10044` |
| Serial type 3 | `2d` | text length `(0x2d - 13) / 2 = 16`, value `Harshita Hirawat` |
| Serial type 4 | `37` | text length `(0x37 - 13) / 2 = 21`, value `SQLite hex navigation` |
| Serial type 5 | `01` | 1-byte integer, value `5b` = 91 |

So the record reconstructed from bytes is:

```text
id = 1
roll_no = 10044
name = Harshita Hirawat
topic = SQLite hex navigation
score = 91
```

The last byte of this row is:

```text
5b
```

`0x5b` is decimal 91, which matches the inserted score.

### Other Row Cells

| Rowid | Cell file offset | Payload size byte | Visible text in dump | Score byte |
|---:|---:|---:|---|---:|
| 1 | `0x1fcd` | `31` | `10044 Harshita Hirawat SQLite hex navigation` | `5b` = 91 |
| 2 | `0x1fa8` | `23` | `10045 Aarav Mehta B-tree cells` | `54` = 84 |
| 3 | `0x1f85` | `21` | `10046 Nisha Rao Page headers` | `58` = 88 |
| 4 | `0x1f60` | `23` | `10047 Kabir Shah Record format` | `4f` = 79 |
| 5 | `0x1f34` | `2a` | `10048 Meera Iyer Varints and payloads` | `5f` = 95 |

## 9. B-tree Structure Observed

This database is very small, so the `students` table fits in one B-tree page. Page 2 is both the root page and a leaf page.

Observed structure:

```text
Database file
|
+-- Page 1
|   +-- SQLite file header, 100 bytes
|   +-- sqlite_schema table leaf page
|       +-- one schema row
|           +-- table name: students
|           +-- rootpage: 2
|
+-- Page 2
    +-- students table root page
    +-- page type: 0x0d, table leaf B-tree page
    +-- 5 cells
        +-- rowid 1 at page offset 0x0fcd
        +-- rowid 2 at page offset 0x0fa8
        +-- rowid 3 at page offset 0x0f85
        +-- rowid 4 at page offset 0x0f60
        +-- rowid 5 at page offset 0x0f34
```

There are no interior B-tree pages yet because five rows are not enough to force a split. If many more rows were inserted, SQLite would eventually create interior pages. Those pages would contain child page numbers and separator keys. In this small file, the only B-tree node for the `students` table is page 2.

## 10. What I Learned

The main thing I noticed is that SQLite is not storing rows as plain CSV-like text. Even though text is visible in the hex dump, every row is wrapped inside a cell. Each table leaf cell has a payload size, a rowid key, a record header, serial types, and then the actual column values.

The page header and cell pointer array are also important. They let SQLite find records without scanning the entire page byte by byte. On page 2, the pointer array at the beginning points to cells stored near the end of the page. This explains how a database page can keep metadata at the front, records at the back, and free space in the middle.

This lab made the B-tree idea more concrete. Page 2 is not just "some data page"; it is a real B-tree node. Since it is a leaf node, it stores row records directly. The schema on page 1 tells SQLite that the `students` table starts at root page 2, and from there the table data can be navigated using the B-tree page header and cell pointers.
