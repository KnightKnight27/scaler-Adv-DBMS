# SQLite Database B-Tree Hex Inspection Report (Lab 4)

**Name:** Rachit S  
**Roll Number:** 24bcs10139  
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Database Overview & Metadata

We are analyzing `students.db`, a SQLite database generated locally.
- **File Size:** 16,384 bytes (exactly 16 KB)
- **Page Size:** 4,096 bytes
- **Total Pages:** 4 pages

### Page Allocation Table
Each page in SQLite represents a specific node in a B-Tree structure. Based on our parser, the 4 pages are allocated as follows:

| Page Number | File Offset Range | B-Tree Page Type | Role / Contents |
| :--- | :--- | :--- | :--- |
| **Page 1** | `0x0000 - 0x0FFF` | Table Leaf (`0x0D`) | Root of the `sqlite_schema` Table B-Tree. Stores schemas and root page references for the database tables and indices. |
| **Page 2** | `0x1000 - 0x1FFF` | Table Leaf (`0x0D`) | Root of the `students` Table B-Tree. Stores the actual table rows (payload). |
| **Page 3** | `0x2000 - 0x2FFF` | Index Leaf (`0x0A`) | Root of the `sqlite_autoindex_students_1` Index B-Tree (Unique index on `student_id`). |
| **Page 4** | `0x3000 - 0x3FFF` | Index Leaf (`0x0A`) | Root of the `sqlite_autoindex_students_2` Index B-Tree (Unique index on `email`). |

---

## 2. Page 1: Root of SQLite Schema Table B-Tree
- **File Offset:** `0x0000 - 0x0FFF`
- **Page Type:** `0x0D` (Table Leaf B-Tree Page)
- **Number of Cells:** 3
- **Start of Cell Content Area:** Byte offset `3703` (relative to page)
- **Cell Pointers Array:** `[3703, 4039, 3990]`

### Database File Header Analysis (First 100 Bytes)
The first 100 bytes of Page 1 contain the SQLite database header:
- `0x00 - 0x0F` (`53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00`): Magic string `"SQLite format 3\0"`
- `0x10 - 0x11` (`10 00`): Database page size (4096 bytes).
- `0x12 - 0x13` (`01 01`): File format write and read version (1 = legacy/default).
- `0x14` (`00`): Reserved space at the end of each page (0 bytes).
- `0x15` (`40`): Maximum embedded payload fraction (64).
- `0x16` (`20`): Minimum embedded payload fraction (32).
- `0x17` (`20`): Leaf payload fraction (32).
- `0x18 - 0x1B` (`00 00 00 02`): File change counter.
- `0x1C - 0x1F` (`00 00 00 04`): Size of database file in pages (4 pages).

### Page 1 B-Tree Header (Starts at Byte 100)
- `0x64` (`0d`): Page type = Table Leaf B-Tree Page.
- `0x65 - 0x66` (`00 00`): First freeblock offset (0 = none).
- `0x67 - 0x68` (`00 03`): Number of cells (3 cells).
- `0x69 - 0x6A` (`0e 77`): Start of cell content area (3703).
- `0x6B` (`00`): Number of fragmented free bytes.

### Deciphering the Schema Cells (Page 1 End)
#### Cell 0 (Offset `3703` / `0x0E77`) — Table `students` Definition
- **Varint Payload Size:** `82 1c` -> Parses to `284` bytes.
- **Varint RowID:** `01` -> RowID 1.
- **Payload Bytes:** Contains schema details.
  - Type: `"table"`
  - Name: `"students"`
  - Table Name: `"students"`
  - Root Page: `02` (page 2)
  - SQL DDL: `CREATE TABLE students ( student_id SERIAL PRIMARY KEY, first_name VARCHAR(100) NOT NULL, last_name VARCHAR(100) NOT NULL, age INT, email VARCHAR(255) UNIQUE, course VARCHAR(100), created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP )`

#### Cell 1 (Offset `4039` / `0x0FC7`) — Index `sqlite_autoindex_students_1`
- **Varint Payload Size:** `2f` -> `47` bytes.
- **Varint RowID:** `02` -> RowID 2.
- **Payload Bytes:**
  - Type: `"index"`
  - Name: `"sqlite_autoindex_students_1"`
  - Table Name: `"students"`
  - Root Page: `03` (page 3)
  - SQL: `NULL` (automatic unique index created for the Primary Key).

#### Cell 2 (Offset `3990` / `0x0F96`) — Index `sqlite_autoindex_students_2`
- **Varint Payload Size:** `2f` -> `47` bytes.
- **Varint RowID:** `03` -> RowID 3.
- **Payload Bytes:**
  - Type: `"index"`
  - Name: `"sqlite_autoindex_students_2"`
  - Table Name: `"students"`
  - Root Page: `04` (page 4)
  - SQL: `NULL` (automatic unique index created for the `UNIQUE` email constraint).

---

## 3. Page 2: Root of `students` Table B-Tree
- **File Offset:** `0x1000 - 0x1FFF`
- **Page Type:** `0x0D` (Table Leaf B-Tree Page)
- **Number of Cells:** 2
- **Start of Cell Content Area:** Byte offset `3943`
- **Cell Pointers Array:** `[4020, 3943]`

### B-Tree Header (Page 2 Start)
- `0x1000` (`0d`): Table Leaf Page.
- `0x1001 - 0x1002` (`00 00`): No freeblocks.
- `0x1003 - 0x1004` (`00 02`): 2 cells (rows).
- `0x1005 - 0x1006` (`0f 67`): Start of cell content area (3943).
- `0x1007` (`00`): Fragmented free bytes.

### Deciphering the Data Cells
SQLite records are stored in a packed binary format. A cell on a Table Leaf page contains:
1. **Varint Payload Size**
2. **Varint RowID**
3. **Record Header:** Starts with header size (varint), followed by serial type codes for each column.
4. **Record Body:** The raw column data.

#### Cell 0 (Offset `4020` / `0x1FB4`) — RowID 1 (Kartik Bhatia)
- **Varint Payload Size:** `4a` -> 74 bytes.
- **Varint RowID:** `01` -> RowID 1.
- **Record Header:** `08 00 19 19 01 31 2d 33`
  - `08`: Header size (8 bytes).
  - `00`: Column 1 (`student_id`): `NULL` (since `student_id` was declared `SERIAL PRIMARY KEY` and no explicit value was inserted, SQLite fell back to `NULL` to save space because the primary key defaults to mapping to RowID).
  - `19`: Column 2 (`first_name`): String of length `(25 - 13)/2 = 6` bytes.
  - `19`: Column 3 (`last_name`): String of length `(25 - 13)/2 = 6` bytes.
  - `01`: Column 4 (`age`): 8-bit signed integer (1 byte).
  - `31`: Column 5 (`email`): String of length `(49 - 13)/2 = 18` bytes.
  - `2d`: Column 6 (`course`): String of length `(45 - 13)/2 = 16` bytes.
  - `33`: Column 7 (`created_at`): String of length `(51 - 13)/2 = 19` bytes.
- **Record Body (Data decoding):**
  - String 1: `4b 61 72 74 69 6b` -> `"Kartik"`
  - String 2: `42 68 61 74 69 61` -> `"Bhatia"`
  - Int: `16` -> `22` (age)
  - String 4: `6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` -> `"kartik@example.com"`
  - String 5: `43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63 65` -> `"Computer Science"`
  - String 6: `32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31` -> `"2026-05-13 21:27:11"`

#### Cell 1 (Offset `3943` / `0x1F67`) — RowID 2 (Prashansa Sharma)
- **Varint Payload Size:** `4b` -> 75 bytes.
- **Varint RowID:** `02` -> RowID 2.
- **Record Header:** `08 00 1f 19 01 37 23 33`
  - `08`: Header size (8 bytes).
  - `00`: Column 1 (`student_id`): `NULL` (defaults to mapping to RowID).
  - `1f`: Column 2 (`first_name`): String of length `(31 - 13)/2 = 9` bytes.
  - `19`: Column 3 (`last_name`): String of length `(25 - 13)/2 = 6` bytes.
  - `01`: Column 4 (`age`): 8-bit signed integer (1 byte).
  - `37`: Column 5 (`email`): String of length `(55 - 13)/2 = 21` bytes.
  - `23`: Column 6 (`course`): String of length `(35 - 13)/2 = 11` bytes.
  - `33`: Column 7 (`created_at`): String of length `(51 - 13)/2 = 19` bytes.
- **Record Body (Data decoding):**
  - String 1: `50 72 61 73 68 61 6e 73 61` -> `"Prashansa"`
  - String 2: `53 68 61 72 6d 61` -> `"Sharma"`
  - Int: `15` -> `21` (age)
  - String 4: `70 72 61 73 68 61 6e 73 61 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` -> `"prashansa@example.com"`
  - String 5: `45 6c 65 63 74 72 6f 6e 69 63 73` -> `"Electronics"`
  - String 6: `32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31` -> `"2026-05-13 21:27:11"`

---

## 4. Page 3: Root of `sqlite_autoindex_students_1` Index B-Tree
- **File Offset:** `0x2000 - 0x2FFF`
- **Page Type:** `0x0A` (Index Leaf B-Tree Page)
- **Number of Cells:** 2
- **Start of Cell Content Area:** Byte offset `4087`
- **Cell Pointers Array:** `[4092, 4087]`

### Index Leaf Cell Format
Unlike table leaf cells, index leaf cells contain no standalone "RowID" field. The RowID is stored inside the payload record itself, appended as the final column. Thus, the index cell is structured as:
1. **Varint Payload Size**
2. **Payload (Record Header + Key Columns + RowID)**

#### Cell 0 (Offset `4092` / `0x2FFC`) — Primary Key Index for RowID 1
- **Varint Payload Size:** `03` -> 3 bytes.
- **Payload Header:** `03` -> 3-byte record header.
- **Serial Type 1 (Key: `student_id`):` `00` -> `NULL` (since `student_id` is null).
- **Serial Type 2 (Value: `RowID`):** `09` -> Serial type `09` represents the constant value **`1`** (the RowID). Because it is a constant, no data bytes are stored in the body.
- **Resulting Payload Bytes:** `03 00` (Header: `03`, Col 1 type: `00`, Col 2 type: `09` which requires 0 bytes).

#### Cell 1 (Offset `4087` / `0x2FF7`) — Primary Key Index for RowID 2
- **Varint Payload Size:** `04` -> 4 bytes.
- **Payload Header:** `03` -> 3-byte record header.
- **Serial Type 1 (Key: `student_id`):** `00` -> `NULL`.
- **Serial Type 2 (Value: `RowID`):** `01` -> 8-bit signed integer (1 byte).
- **Payload Body:** `02` -> RowID = 2.
- **Resulting Payload Bytes:** `03 00 01 02` (Header: `03 00 01`, Body: `02`).

---

## 5. Page 4: Root of `sqlite_autoindex_students_2` Index B-Tree
- **File Offset:** `0x3000 - 0x3FFF`
- **Page Type:** `0x0A` (Index Leaf B-Tree Page)
- **Number of Cells:** 2
- **Start of Cell Content Area:** Byte offset `4048`
- **Cell Pointers Array:** `[4074, 4048]`

#### Cell 0 (Offset `4074` / `0x3FEA`) — Unique Email Index for RowID 1
- **Varint Payload Size:** `15` -> 21 bytes.
- **Payload Header:** `03` -> 3-byte record header.
- **Serial Type 1 (Key: `email`):** `31` -> String of length `(49 - 13)/2 = 18` bytes.
- **Serial Type 2 (Value: `RowID`):** `09` -> Constant integer `1` (requiring 0 bytes of body data).
- **Payload Body:** `6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` -> `"kartik@example.com"`.

#### Cell 1 (Offset `4048` / `0x3FD0`) — Unique Email Index for RowID 2
- **Varint Payload Size:** `19` -> 25 bytes.
- **Payload Header:** `03` -> 3-byte record header.
- **Serial Type 1 (Key: `email`):** `37` -> String of length `(55 - 13)/2 = 21` bytes.
- **Serial Type 2 (Value: `RowID`):** `01` -> 8-bit signed integer (1 byte).
- **Payload Body:**
  - String: `70 72 61 73 68 61 6e 73 61 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` -> `"prashansa@example.com"`
  - RowID byte: `02` -> RowID = 2.

---

## 6. Conclusion
By parsing the raw bytes of `students.db`, we have successfully mapped out the SQLite B-Tree database structure. This demonstrates:
1. How SQLite groups table data and indices into distinct pages.
2. How leaf nodes store packed schemas, tables, and indices inside space-optimized cell structures.
3. How SQLite's variable-length integer (varint) encoding and serial type representations allow records to be packed compactly into binary storage.
