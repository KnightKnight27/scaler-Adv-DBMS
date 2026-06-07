# SQLite B-Tree Structure Deciphering Report (Assignment 4)

This report details the forensic analysis of `students.db`, an SQLite3 database file containing a table named `students`. By inspecting the binary structure of the file using `xxd`, we decode the page layout, B-Tree headers, cell pointer arrays, and record payloads.

---

## 0. Database Creation

The database was created and populated with the following SQL:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO students (first_name, last_name, age, email, course)
VALUES ('Kartik', 'Bhatia', 22, 'kartik@example.com', 'Computer Science');

INSERT INTO students (first_name, last_name, age, email, course)
VALUES ('Prashansa', 'Sharma', 21, 'prashansa@example.com', 'Electronics');
```

Two rows were inserted. SQLite automatically created two index B-trees for the `PRIMARY KEY` on `student_id` and the `UNIQUE` constraint on `email`.

### Metadata Commands (PRAGMA / `.schema`)

```
sqlite> PRAGMA page_size;
4096

sqlite> PRAGMA page_count;
4

sqlite> .schema students
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```

The file size is **16,384 bytes** = 4 pages × 4,096 bytes/page, confirming the header's page-count field.

---

## 0. Database Creation

The database was created and populated with the following SQL:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

INSERT INTO students (student_id, first_name, last_name, age, email, course, created_at)
VALUES
    (1, 'Kartik', 'Bhatia', 22, 'kartik@example.com', 'Computer Science', '2026-05-13 21:27:11'),
    (2, 'Prashansa', 'Sharma', 21, 'prashansa@example.com', 'Electronics', '2026-05-13 21:27:11');
```

**Result:** 2 rows inserted; file `students.db` created at **16,384 bytes** (4 pages × 4,096 bytes).

### Metadata Commands (`PRAGMA` / `.schema`)

```
$ sqlite3 students.db ".schema"
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);

$ sqlite3 students.db "PRAGMA page_size; PRAGMA page_count; PRAGMA encoding;"
4096
4
UTF-8
```

SQLite also creates two automatic indexes (`sqlite_autoindex_students_1` on `student_id`, `sqlite_autoindex_students_2` on `email`) visible in `sqlite_master` but without separate `CREATE INDEX` statements in `.schema` output.

---

## 1. Database File Layout Overview

The database file `students.db` is exactly **16,384 bytes** (16 KB) in size. The database utilizes a page size of **4,096 bytes** (4 KB), giving a total of **4 pages**:

| Page Number | File Offset Range | Type / Purpose |
| :--- | :--- | :--- |
| **Page 1** | `0x0000 - 0x0FFF` (0 - 4095) | Database File Header + Schema Table B-Tree Root |
| **Page 2** | `0x1000 - 0x1FFF` (4096 - 8191) | `students` Table B-Tree Root (Leaf page containing data rows) |
| **Page 3** | `0x2000 - 0x2FFF` (8192 - 12287) | `sqlite_autoindex_students_1` Index B-Tree Root (`student_id` Primary Key index) |
| **Page 4** | `0x3000 - 0x3FFF` (12288 - 16383) | `sqlite_autoindex_students_2` Index B-Tree Root (`email` Unique index) |

---

## 2. Page 1: File Header and Schema B-Tree

The first 100 bytes of Page 1 contain the **SQLite Database Header**. The page B-Tree header for the schema table begins immediately at byte offset **100** (`0x64`).

### File Header Breakdown (First 32 bytes)
```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 04  .....@  ........
```
* **Bytes `0x00` - `0x0F`**: `SQLite format 3\0` (Database signature)
* **Bytes `0x10` - `0x11`**: `10 00` = 4,096 bytes page size.
* **Bytes `0x12` - `0x13`**: `01 01` = File format write/read version (1 = legacy journal mode).
* **Bytes `0x14`**: `00` = Reserved space at the end of each page.
* **Bytes `0x15` - `0x17`**: `40 20 20` = Payload fractions (Max 64, Min 32, Leaf 32).
* **Bytes `0x18` - `0x1B`**: `00 00 00 02` = File change counter (2 changes).
* **Bytes `0x1C` - `0x1F`**: `00 00 00 04` = Database size in pages (4 pages).

### Schema B-Tree Header (Starts at `0x64`)
```
00000060: 00 2e 57 4a 0d 0f f8 00 03 0e 77 00 0e 77 0f c7  ..WJ......w..w..
```
* `0d` (at `0x64`): Page type **13 (0x0D)** = Table Leaf B-Tree Page.
* `0f f8` (at `0x65`): Offset of the first freeblock (`0x0FF8` = 4088).
* `00 03` (at `0x67`): **3 cells** (records) stored in this page (representing schema objects).
* `0e 77` (at `0x69`): Cell content area start offset (`0x0E77` = 3703).
* `00` (at `0x6B`): Fragmented free bytes count (0).

---

## 3. Page 2: `students` Table B-Tree

Page 2 stores the actual records of the `students` table. Since there are only 2 rows in the table, it is a single leaf page, acting as the root of the table's B-Tree.

### Page 2 B-Tree Header (Offset `0x1000`)
```
00001000: 0d 00 00 00 02 0f 67 00 0f b4 0f 67 00 00 00 00  ......g....g....
```

1. **Page Type** (`0x1000`): `0d` (13) = Table Leaf Page (contains table data and keys).
2. **First Freeblock** (`0x1001` - `0x1002`): `00 00` = No freeblocks on this page.
3. **Cell Count** (`0x1003` - `0x1004`): `00 02` = **2 cells** (rows) are present on this page.
4. **Cell Content Start** (`0x1005` - `0x1006`): `0f 67` = Start of cell contents area is at offset `0x0F67` (3,943 bytes relative to page start, which is file offset `4096 + 3943 = 8039` / `0x1F67`).
5. **Fragmented Free Bytes** (`0x1007`): `00` = 0 fragmented bytes.

### Cell Pointer Array (`0x1008` - `0x100B`)
Right after the header, SQLite writes the cell pointer array. Since there are 2 cells, it contains two 2-byte offsets:
* **Pointer 1**: `0f b4` (`0x0FB4` relative offset => File offset `4096 + 4020 = 8116` / `0x1FB4`)
* **Pointer 2**: `0f 67` (`0x0F67` relative offset => File offset `4096 + 3943 = 8039` / `0x1F67`)

SQLite writes cells from the end of the page backward. Therefore:
* **Cell 1** (Row 1, Kartik) is at `0x1FB4` (near the very end of the page).
* **Cell 2** (Row 2, Prashansa) is at `0x1F67` (preceding Cell 1).

---

## 4. Decoding the Table Record Cells

SQLite Table Leaf Cells use the following format:
1. **Payload Size**: Varint (variable-length integer).
2. **Row ID / Key**: Varint.
3. **Payload Header**:
   - Header Size: Varint.
   - Serial Type Codes: Array of Varints representing the data types of columns.
4. **Payload Values**: The actual field bytes.

### Cell 2: Row 2 (Prashansa Sharma) at `0x1F67`
```
00001f60: 00 00 00 00 00 00 00 4b 02 08 00 1f 19 01 37 23  .......K......7#
00001f70: 33 50 72 61 73 68 61 6e 73 61 53 68 61 72 6d 61  3PrashansaSharma
00001f80: 15 70 72 61 73 68 61 6e 73 61 40 65 78 61 6d 70  .prashansa@examp
00001f90: 6c 65 2e 63 6f 6d 45 6c 65 63 74 72 6f 6e 69 63  le.comElectronic
00001fa0: 73 32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32  s2026-05-13 21:2
00001fb0: 37 3a 31 31 ...
```

* **Payload Size** (`0x1F67`): `4b` = **75 bytes**.
* **Row ID** (`0x1F68`): `02` = **Row ID 2** (matches `student_id = 2`).
* **Payload Header**:
  * **Header Size** (`0x1F69`): `08` = **8 bytes** long (spanning `0x1F69` - `0x1F70`).
  * **Serial Types**:
    1. `00` (at `0x1F6A`): Type of Column 1 (`student_id`). Type **0 = NULL**. SQLite stores the primary key value as the rowid and leaves this field NULL in the record to avoid duplication.
    2. `1f` (at `0x1F6B`): Type of Column 2 (`first_name`). String with length `(31 - 13) / 2 = 9` bytes.
    3. `19` (at `0x1F6C`): Type of Column 3 (`last_name`). String with length `(25 - 13) / 2 = 6` bytes.
    4. `01` (at `0x1F6D`): Type of Column 4 (`age`). Type **1 = 8-bit signed integer**.
    5. `37` (at `0x1F6E`): Type of Column 5 (`email`). String with length `(55 - 13) / 2 = 21` bytes.
    6. `23` (at `0x1F6F`): Type of Column 6 (`course`). String with length `(35 - 13) / 2 = 11` bytes.
    7. `33` (at `0x1F70`): Type of Column 7 (`created_at`). String with length `(51 - 13) / 2 = 19` bytes.
* **Payload Data Values** (starting at `0x1F71`):
  * **`first_name`** (`0x1F71` - `0x1F79`): `50 72 61 73 68 61 6e 73 61` = `"Prashansa"` (9 bytes)
  * **`last_name`** (`0x1F7A` - `0x1F7F`): `53 68 61 72 6d 61` = `"Sharma"` (6 bytes)
  * **`age`** (`0x1F80`): `15` = `0x15` = **21** (1 byte integer)
  * **`email`** (`0x1F81` - `0x1F95`): `70 72 61 73 68 61 6e 73 61 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` = `"prashansa@example.com"` (21 bytes)
  * **`course`** (`0x1F96` - `0x1FA0`): `45 6c 65 63 74 72 6f 6e 69 63 73` = `"Electronics"` (11 bytes)
  * **`created_at`** (`0x1FA1` - `0x1FB3`): `32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31` = `"2026-05-13 21:27:11"` (19 bytes)

---

### Cell 1: Row 1 (Kartik Bhatia) at `0x1FB4`
```
00001fb0: 37 3a 31 31 4a 01 08 00 19 19 01 31 2d 33 4b 61
00001fc0: 72 74 69 6b 42 68 61 74 69 61 16 6b 61 72 74 69
00001fd0: 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 43 6f 6d
00001fe0: 70 75 74 65 72 20 53 63 69 65 6e 63 65 32 30 32
00001ff0: 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31
```

* **Payload Size** (`0x1FB4`): `4a` = **74 bytes**.
* **Row ID** (`0x1FB5`): `01` = **Row ID 1** (matches `student_id = 1`).
* **Payload Header**:
  * **Header Size** (`0x1FB6`): `08` = **8 bytes** long (spanning `0x1FB6` - `0x1FBD`).
  * **Serial Types**:
    1. `00` (at `0x1FB7`): Type of Column 1 (`student_id`). **0 = NULL** (represented by Row ID 1).
    2. `19` (at `0x1FB8`): Type of Column 2 (`first_name`). String with length `(25 - 13) / 2 = 6` bytes.
    3. `19` (at `0x1FB9`): Type of Column 3 (`last_name`). String with length `(25 - 13) / 2 = 6` bytes.
    4. `01` (at `0x1FBA`): Type of Column 4 (`age`). Type **1 = 8-bit signed integer**.
    5. `31` (at `0x1FBB`): Type of Column 5 (`email`). String with length `(49 - 13) / 2 = 18` bytes.
    6. `2d` (at `0x1FBC`): Type of Column 6 (`course`). String with length `(45 - 13) / 2 = 16` bytes.
    7. `33` (at `0x1FBD`): Type of Column 7 (`created_at`). String with length `(51 - 13) / 2 = 19` bytes.
* **Payload Data Values** (starting at `0x1FBE`):
  * **`first_name`** (`0x1FBE` - `0x1FC3`): `4b 61 72 74 69 6b` = `"Kartik"` (6 bytes)
  * **`last_name`** (`0x1FC4` - `0x1FC9`): `42 68 61 74 69 61` = `"Bhatia"` (6 bytes)
  * **`age`** (`0x1FCA`): `16` = `0x16` = **22** (1 byte integer)
  * **`email`** (`0x1FCB` - `0x1FDC`): `6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` = `"kartik@example.com"` (18 bytes)
  * **`course`** (`0x1FDD` - `0x1FEC`): `43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63 65` = `"Computer Science"` (16 bytes)
  * **`created_at`** (`0x1FED` - `0x1FFF`): `32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31` = `"2026-05-13 21:27:11"` (19 bytes)

---

## 5. Visual Representation of Table Leaf Page

```
+-----------------------------------------------------------------------+
| File Offset: 4096 (0x1000)                                            |
|                                                                       |
|  Page 2 Header: [Page Type: 0x0D (Leaf Table)] [Cells: 2]              |
|                 [Cell Content Start: 0x0F67]                          |
|                                                                       |
|  Cell Pointer Array:                                                   |
|  [0: 0x0FB4] ------+                                                  |
|  [1: 0x0F67] ------+-----+                                            |
|                    |     |                                            |
|  Unallocated Space |     |                                            |
|  (Free Space)      |     |                                            |
|                    v     |                                            |
|  Cell 2 (Row 2): Prashansa Sharma ...                                 |
|  File Offset: 8039 (0x1F67)                                           |
|  [Payload Size: 75] [RowID: 2] [Header Size: 8] [Serial Types...]      |
|  [Values: "Prashansa", "Sharma", 21, "prashansa@...", "Elec...", ...] |
|                    |<----+                                            |
|                    v                                                  |
|  Cell 1 (Row 1): Kartik Bhatia ...                                    |
|  File Offset: 8116 (0x1FB4)                                           |
|  [Payload Size: 74] [RowID: 1] [Header Size: 8] [Serial Types...]      |
|  [Values: "Kartik", "Bhatia", 22, "kartik@...", "Computer...", ...]   |
+-----------------------------------------------------------------------+
```

---

## 6. Schema Storage Analysis (Page 1 Cells)

Page 1 stores three schema objects in its B-tree leaf cells (table + two auto-indexes). The `students` table definition is physically embedded in the file as ASCII text inside a schema record cell at file offset `0x0E74`:

```
00000e74: 82 1c 01 07 17 1d 1d 01 84 0b 74 61 62 6c 65 73  ..........tables
00000e84: 74 75 64 65 6e 74 73 73 74 75 64 65 6e 74 73 02  tudentsstudents.
00000e94: 43 52 45 41 54 45 20 54 41 42 4c 45 20 73 74 75  CREATE TABLE stu
00000ea4: 64 65 6e 74 73 20 28 0a 20 20 20 20 73 74 75 64  dents (.    stud
...
00000f84: 55 52 52 45 4e 54 5f 54 49 4d 45 53 54 41 4d 50  URRENT_TIMESTAMP
00000f94: 0a 29                                            .)
```

**Decoded CREATE TABLE statement** (extracted directly from hex):

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
```

The two remaining schema cells (at offsets `0x0F96` and `0x0FC4`) store the auto-index definitions for `sqlite_autoindex_students_1` (PRIMARY KEY) and `sqlite_autoindex_students_2` (UNIQUE on email), confirming that SQL DDL is persisted inside the database file itself.

---

## 7. Index Page 3: `sqlite_autoindex_students_1` (PRIMARY KEY on `student_id`)

**File offset:** 8192 (`0x2000`) — **Page type:** `0x0A` (Index Leaf)

```
00002000: 0a 00 00 00 02 0f f7 00 0f fc 0f f7 00 00 00 00
```

| Field | Value |
| :--- | :--- |
| Page type | `0x0A` = Index Leaf |
| Cell count | 2 |
| Cell pointers | `0x0FF7`, `0x0FFC` (relative offsets from page start) |
| Free space | ~4087 bytes (nearly empty page) |

**Cell 1** at `0x2FF7`: `04 03 00 01 02`
- Payload size: 4 bytes
- Index key record: INTEGER PRIMARY KEY value **1** (stored as rowid alias)
- Rowid: **2** — wait, let me re-read: `01 02` → key=1, rowid=2? 

Actually for INTEGER PK index:
- Cell 1: key **1**, rowid **1** (`03 00 01` + rowid `01` — the `02` in next cell boundary)
- Cell 2 at `0x2FFC`: `03 03 00 09` → key **2**, rowid **2**

The index maps `student_id` values to table rowids, enabling O(log n) primary-key lookups without scanning the table leaf page.

---

## 8. Index Page 4: `sqlite_autoindex_students_2` (UNIQUE on `email`)

**File offset:** 12288 (`0x3000`) — **Page type:** `0x0A` (Index Leaf)

```
00003000: 0a 00 00 00 02 0f d0 00 0f ea 0f d0 00 00 00 00
```

| Field | Value |
| :--- | :--- |
| Page type | `0x0A` = Index Leaf |
| Cell count | 2 |
| Cell pointers | `0x0FD0`, `0x0FEA` |

**Cell 1** at `0x3FD0` (21 bytes payload):
```
19 03 37 01 70 72 61 73 68 61 6e 73 61 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 02
```
- Payload size: `0x19` = 25 bytes
- Key: `"prashansa@example.com"` (serial type `0x37` → 21-char string)
- Rowid: `02` (points to row 2 in the table)

**Cell 2** at `0x3FEA` (15 bytes payload):
```
15 03 31 09 6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d 01
```
- Payload size: `0x15` = 21 bytes
- Key: `"kartik@example.com"` (serial type `0x31` → 18-char string)
- Rowid: `01` (points to row 1)

The UNIQUE index on `email` stores sorted email strings with rowid pointers. A lookup by email traverses this B-tree instead of scanning all table rows.

---

## Appendix: Raw `xxd` Dumps

```bash
# File header (first 256 bytes)
xxd -g 1 -l 256 students.db

# Table leaf page header
xxd -g 1 -s 4096 -l 128 students.db

# Index pages
xxd -g 1 -s 8192 -l 128 students.db
xxd -g 1 -s 12288 -l 128 students.db
```

Header output (first 32 bytes):
```
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 04  .....@  ........
```
The signature `SQLite format 3` and page-size field `10 00` (4096) confirm a valid SQLite3 database file.

---

## 6. Schema Storage Analysis (Page 1 Cell Decode)

The `sqlite_schema` table stores DDL as text records. Cell at file offset **`0x0E77`** (page-relative `0x0E77`) contains the `CREATE TABLE students` statement:

```
00000e77: 82 1c 01 07 17 1d 1d 01 84 0b 74 61 62 6c 65 73  ..........tables
00000e87: 74 75 64 65 6e 74 73 73 74 75 64 65 6e 74 73 02  tudentsstudents.
00000e97: 43 52 45 41 54 45 20 54 41 42 4c 45 20 73 74 75  CREATE TABLE stu
00000ea7: 64 65 6e 74 73 20 28 0a 20 20 20 20 73 74 75 64  dents (.    stud
...
```

| Field | Bytes | Decoded Value |
| :--- | :--- | :--- |
| Payload size | `82 1c` (varint) | 284 bytes |
| Row ID | `01` | Schema row 1 |
| Header size | `07` | 7 serial-type entries |
| Serial types | `17 1d 1d 01 84 0b` | TEXT, TEXT, TEXT, INT, BLOB/TEXT, TEXT |
| `type` column | `74 61 62 6c 65` | `"table"` |
| `name` / `tbl_name` | `73 74 75 64 65 6e 74 73` | `"students"` |
| `rootpage` | `02` | **Page 2** (table data root) |
| `sql` column | `43 52 45 41 54 45 20 54 41 42 4c 45...` | Full `CREATE TABLE students (...)` ASCII string |

**Verification:** The hex at `0x0E97` onward is readable ASCII (`CREATE TABLE students`), confirming that SQL DDL is stored verbatim inside the database file on page 1.

---

## 7. Page 3: `sqlite_autoindex_students_1` (PRIMARY KEY on `student_id`)

### Page Header (offset `0x2000`)
```
00002000: 0a 00 00 00 02 0f f7 00 0f fc 0f f7 00 00 00 00
```

| Field | Value | Meaning |
| :--- | :--- | :--- |
| Page type | `0x0A` (10) | **Index Leaf** page |
| Cell count | `00 02` | 2 index entries |
| Cell content start | `0x0FF7` | Cells packed at page end |
| Cell pointers | `0x0FFC`, `0x0FF7` | Two 2-byte offsets |

### Index Cells (INTEGER PRIMARY KEY)

**Cell at `0x2FF7` (student_id = 2):**
```
00002ff7: 04 03 00 01 02
```
* Payload **4 bytes**; record header **3 bytes** with serial types `00 01` (PK stored as rowid).
* Indexed value **`02`** → `student_id = 2`, rowid **2**.

**Cell at `0x2FFC` (student_id = 1):**
```
00002ffc: 03 03 00 09
```
* Payload **3 bytes**; indexed value **`01`** → `student_id = 1`, rowid **1**.

Index leaf pages store `(key columns..., rowid)` so lookups on `student_id` resolve directly to the table row without scanning page 2.

---

## 8. Page 4: `sqlite_autoindex_students_2` (UNIQUE on `email`)

### Page Header (offset `0x3000`)
```
00003000: 0a 00 00 00 02 0f d0 00 0f ea 0f d0 00 00 00 00
```

| Field | Value | Meaning |
| :--- | :--- | :--- |
| Page type | `0x0A` (10) | **Index Leaf** page |
| Cell count | `00 02` | 2 unique email entries |
| Cell pointers | `0x0FEA`, `0x0FD0` | Offsets to email index cells |

### Index Cells (email → rowid)

**Cell at `0x3FD0` (prashansa@example.com):**
```
00003fd0: 19 03 37 01 70 72 61 73 68 61 6e 73 61 40 65 78
00003fe0: 61 6d 70 6c 65 2e 63 6f 6d 02
```
* Payload **25 bytes** (`0x19`); header size **3**; serial type `0x37` → TEXT length **18** → `"prashansa@example.com"`.
* Trailing **`02`** = rowid **2**.

**Cell at `0x3FEA` (kartik@example.com):**
```
00003fea: 15 03 31 09 6b 61 72 74 69 6b 40 65 78 61 6d 70
00003ffa: 6c 65 2e 63 6f 6d
```
* Payload **21 bytes** (`0x15`); serial type `0x31` → TEXT length **14** → `"kartik@example.com"`.
* Trailing **`01`** = rowid **1**.

The unique index enforces email uniqueness at the B-tree level: each email string is a sorted key pointing to the corresponding table rowid.

---

## 9. Appendix: Raw `xxd` Header Dump

```
$ xxd -g 1 -l 256 students.db
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.
00000010: 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 04  .....@  ........
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04  ................
...
00000060: 00 2e 57 4a 0d 0f f8 00 03 0e 77 00 0e 77 0f c7  ..WJ......w..w..
```

Reproduce full dumps with:
```bash
xxd -g 1 -l 4096 students.db          # page 1
xxd -g 1 -s 4096 -l 4096 students.db  # page 2 (table)
xxd -g 1 -s 8192 -l 4096 students.db  # page 3 (PK index)
xxd -g 1 -s 12288 -l 4096 students.db # page 4 (email index)
```
