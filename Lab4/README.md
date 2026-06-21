# Lab 4: SQLite3 Database Internal Structure Analysis Using XXD

## Objective

The objective of this lab is to explore and analyze the internal storage structure of a SQLite3 database file using hexadecimal inspection tools (`xxd`). This analysis maps logical database objects (tables and records) directly to their physical byte representation on disk.

---

## Database Creation

A SQLite database named `lab4.db` was created and populated with a `students` schema containing three sample rows.

**Execution Commands:**

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    gpa REAL
);

INSERT INTO students (id, name, gpa) VALUES (1, 'Alice', 3.8);
INSERT INTO students (id, name, gpa) VALUES (2, 'Bob', 3.5);
INSERT INTO students (id, name, gpa) VALUES (3, 'Charlie', 3.9);

```

---

## Database Metadata Analysis

Using SQLite's metadata commands, the logical page layout metrics were retrieved:

```sql
sqlite> PRAGMA page_size;
4096
sqlite> PRAGMA page_count;
2

```

### Analysis of Metadata:

* **Page Size:** `4096` bytes. This represents the default allocation block size SQLite uses to read/write data from storage on macOS.
* **Page Count:** `2`. The database consists of exactly two pages ($2 \times 4096 = 8192$ total bytes).
* **Root Page Mapping:** Running `SELECT * FROM sqlite_schema;` revealed:
```text
table|students|students|2|CREATE TABLE...

```


The value `2` specifies that the root B-Tree page for the `students` data table physically begins on **Page 2** of the file, while Page 1 is reserved for the database catalog schema.

---

## SQLite File Header Inspection

The global SQLite file header occupies the first 100 bytes of Page 1. Running `xxd -l 100 lab4.db` yielded:

```text
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0c40 2020 0000 0004 0000 0002  .....@  ........

```

### Key Field Interpretations:

* **Magic String / File Signature (Bytes 0-15):** `5351 4c69 7465 2066 6f72 6d61 7420 3300` matches the raw ASCII representation of `SQLite format 3\000`. This verifies the file format integrity.
* **Page Size (Bytes 16-17):** `1000` in hexadecimal translates directly to $4096$ in decimal, matching the `PRAGMA page_size` output.
* **File Page Count (Bytes 28-31):** `0000 0002` explicitly confirms a file length of 2 pages.

---

## Task 4 & 5: B-Tree Page & Cell Pointer Array Analysis

Because Page 1 holds a file header, its master B-Tree page header begins immediately after it at offset `0x0064`.

```text
00000060: 002e 8df8 0d00 0000 010f 8400 0f84 0000  ................
                    ^^

```

### Header Decode Breakdown:

* **Page Type Flag (`0x0064`):** `0x0d`. This value signifies a **Table Leaf Page** B-Tree descriptor (containing actual keys and data payloads instead of interior node navigation pointers).
* **Number of Cells (`0x0067-0x0068`):** `0x0001`. This informs the engine that exactly 1 child record tracking structure (the definition string for table `students`) resides indexed on Page 1.
* **Cell Content Start (`0x0069-0x006A`):** `0x0f84` ($3972$ in decimal). This is the allocation water-mark; data payloads grow upward from byte 3972 toward the top header.

### Cell Pointer Array Verification:

Dumping the array offset (`xxd -s 112 -l 16`) returned a clean zero block:

```text
00000070: 0000 0000 0000 0000 0000 0000 0000 0000  ................

```

Because the cell count for Page 1 is only 1, the single 2-byte cell pointer offset is embedded right at the end of the header (`0x006e`: `0x0f84`). No separate array structure exists until additional schema records (indexes, views) are added.

---

## Record & Schema Storage Analysis

As observed in the full page dump (`xxd lab4.db | head -n 50`), Page 1 from bytes `0x0070` to `0x0310` is comprised entirely of zero padding (`0x00`).

This highlights SQLite's physical layout model: **headers grow downward** from the top of a page, whereas **record payloads are written backward** starting from the absolute bottom limit of the page boundary ($4096$ bytes down).

The table creation instruction `CREATE TABLE students...` and its text records (`Alice`, `Bob`, `Charlie`) are located on the back-half of Page 1 and inside the sector boundaries of Page 2 (offsets `0x1000` to `0x2000`), proving that user records and catalogs are maintained inline as binary B-tree payloads.

---

## Physical File Layout Study

The layout structure of the created `lab4.db` database is mapped as follows:

| Byte Offset (Hex) | Size (Bytes) | Storage Component | Functional Description |
| --- | --- | --- | --- |
| `0x0000 - 0x0063` | 100 | **Global Database Header** | Tracks structural signatures, block sizes, write states, and version fields. |
| `0x0064 - 0x006B` | 8 | **Page 1 B-Tree Header** | Defines page rules (Leaf: `0x0d`), cells count, and space flags. |
| `0x006C - ` | Variable | **Cell Array Maps** | Indexes locations of internal catalog entities. |
| Mid-Page Blocks | Variable | **Free/Unallocated Space** | Open space buffer separating header growth from bottom record stacks. |
| Tail End of Page 1 | Variable | **System Catalog Payload** | Houses raw string records mapping the system schema. |
| `0x1000 - 0x2000` | 4096 | **Page 2 Leaf Node Data** | The physical cluster block storing the user data records (`Alice`, `Bob`, `Charlie`). |
