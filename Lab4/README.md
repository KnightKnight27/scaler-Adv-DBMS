# Lab 4 - SQLite3 Hex Dump

used `xxd students.db` to inspect the raw bytes of the database file.

## the database

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name  VARCHAR(100) NOT NULL,
    age        INT,
    email      VARCHAR(255) UNIQUE,
    course     VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

two rows inserted:
- Kartik Bhatia, 22, kartik@example.com, Computer Science
- Prashansa Sharma, 21, prashansa@example.com, Electronics

---

## how sqlite lays out the file

sqlite splits the file into 4096-byte pages. each page is either a table page (stores rows) or an index page (stores index keys). our db has 4 pages total:

```
0x0000 - page 1  -> schema (sqlite_master)
0x1000 - page 2  -> students table data
0x2000 - page 3  -> unique index on email
0x3000 - page 4  -> second unique index
```

---

## page 1 - the header (0x0000)

the first 100 bytes of every sqlite file is a fixed header. when i ran xxd this is what i saw:

```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0004  .....@  ........
```

breaking it down:
- `0x0000` - the string "SQLite format 3" - just confirms its a sqlite file
- `0x0010` - `10 00` = 4096, the page size
- `0x0012` - `01` write version, `01` read version
- `0x0018` - `00000002` file change counter (been written twice)
- `0x001C` - `00000004` total pages = 4
- `0x0060` - `002E574A` sqlite version number

after the 100 byte header, the b-tree header for page 1 starts at `0x0064`:

```
00000060: 002e 574a 0d0f f800 030e 7700 0e77 0fc7  ..WJ......w..w..
```

- `0x0064` = `0D` -> this is a leaf table page
- `0x0067` = `0003` -> 3 cells on this page (one per schema entry)
- `0x0069` = `0E77` -> cell content starts at offset 0x0E77

the 3 cells are the schema rows (sqlite_master). pointers to them sit right after the header:

| pointer | offset | what it points to |
|---------|--------|-------------------|
| 0x0E77  | 3703   | students table definition |
| 0x0FC7  | 4039   | auto index 1 |
| 0x0F96  | 3990   | auto index 2 |

at 0x0E77 you can see the schema row in the dump:

```
00000e80: 0b74 6162 6c65 7374 7564 656e 7473 7374  .tablestudentsst
00000e90: 7564 656e 7473 0243 5245 4154 4520 5441  udents.CREATE TA
00000ea0: 424c 4520 7374 7564 656e 7473 2028 0a20  BLE students (. 
```

you can literally read "tablestudentsstudents" and then the full CREATE TABLE sql. the record stores: type, name, tbl_name, rootpage (=2, meaning the actual student rows are on page 2), and the sql text.

---

## page 2 - actual student rows (0x1000)

```
00001000: 0d00 0000 020f 6700 0fb4 0f67 ...
```

- `0x1000` = `0D` -> leaf table page again
- `0x1003` = `0002` -> 2 rows
- `0x1005` = `0F67` -> content starts at 0x0F67 within the page (= 0x1F67 in the file)

cell pointers:
- `0x0FB4` -> file offset `0x1FB4` -> Kartik's row
- `0x0F67` -> file offset `0x1F67` -> Prashansa's row

**Kartik's row at 0x1FB4:**

```
00001fc0: 7274 696b 4268 6174 6961 166b 6172 7469  rtikBhatia.karti
00001fd0: 6b40 6578 616d 706c 652e 636f 6d43 6f6d  k@example.comCom
00001fe0: 7075 7465 7220 5363 6965 6e63 6532 3032  puter Science202
00001ff0: 362d 3035 2d31 3320 3231 3a32 373a 3131  6-05-13 21:27:11
```

record header bytes:
- `0x4A` = 74 bytes payload
- `0x01` = rowid 1
- `0x08` = header is 8 bytes long
- column types: NULL, text(6), text(6), int1, text(18), text(16), text(19)
- data: Kartik, Bhatia, age=22, kartik@example.com, Computer Science, timestamp

**Prashansa's row at 0x1F67:**

```
00001f70: 3350 7261 7368 616e 7361 5368 6172 6d61  3PrashansaSharma
00001f80: 1570 7261 7368 616e 7361 4065 7861 6d70  .prashansa@examp
```

- `0x4B` = 75 bytes payload
- `0x02` = rowid 2
- data: Prashansa, Sharma, age=21, prashansa@example.com, Electronics, timestamp

---

## pages 3 & 4 - the indexes (0x2000, 0x3000)

sqlite automatically created two indexes because of the UNIQUE constraint on email. page type here is `0x0A` (leaf index page, not table).

```
00002000: 0a00 0000 020f f700 0ffc 0ff7 ...
00003000: 0a00 0000 020f d000 0fea 0fd0 ...
```

the index entries at the bottom of page 4:

```
00003fd0: 1903 3701 7072 6173 6861 6e73 6140 6578  ..7.prashansa@ex
00003fe0: 616d 706c 652e 636f 6d02 1503 3109 6b61  ample.com...1.ka
00003ff0: 7274 696b 4065 7861 6d70 6c65 2e63 6f6d  rtik@example.com
```

each entry is: `[key string][rowid]`
- "prashansa@example.com" -> rowid 2
- "kartik@example.com" -> rowid 1

when you do `WHERE email = 'kartik@example.com'`, sqlite hits this index first, gets rowid=1, then goes directly to page 2 to fetch the full row.

---

## b-tree structure

```
Page 1 (sqlite_master, leaf table, 0x0D)
  cell[0] -> students table, rootpage = 2
  cell[1] -> autoindex_1,    rootpage = 3
  cell[2] -> autoindex_2,    rootpage = 4
       |                  |
       v                  v
  Page 2              Page 3 & 4
  (0x0D leaf)         (0x0A leaf index)
  rowid=1 Kartik      "kartik@..."    -> 1
  rowid=2 Prashansa   "prashansa@..." -> 2
```

since we only have 2 rows, everything is a leaf node - no interior nodes. if we had thousands of rows sqlite would add interior nodes (type `0x05` for table, `0x02` for index) to split the tree.

node type reference:
- `0x0D` = leaf table (has row data)
- `0x0A` = leaf index (has key + rowid)
- `0x05` = interior table (has child page pointers)
- `0x02` = interior index (has child page pointers)

---

## address summary

| address | description |
|---------|-------------|
| 0x0000 | file header starts |
| 0x0064 | page 1 b-tree header |
| 0x006C | cell pointer array (3 pointers) |
| 0x0E77 | students table schema row |
| 0x0F96 | autoindex_2 schema row |
| 0x0FC7 | autoindex_1 schema row |
| 0x1000 | page 2 - students data |
| 0x1F67 | Prashansa's row |
| 0x1FB4 | Kartik's row |
| 0x2000 | page 3 - index leaf |
| 0x3000 | page 4 - index leaf |
| 0x3FD0 | index key entries |

full dump: [students_hexdump.txt](students_hexdump.txt)
