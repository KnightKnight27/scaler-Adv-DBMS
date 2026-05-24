# Advanced DBMS Lab - 4: SQLite3 Page Navigation & B-Tree Hex Dump Analysis

## Objective

The goal of this lab session was to tear down a SQLite3 database file at the byte level to understand how it actually manages storage under the hood. Using the `xxd` hex dump utility, I analyzed a custom-built database (`students.db`) to identify and decode:

* Database file headers and page size parameters
* B-tree page types and leaf layout structures
* The cell pointer array configuration
* How variable-length record payloads are packed onto a physical disk page
* How internal indexing metadata maps out to speed up row lookups

---

## Database Setup & Data Insertion

I initialized the database environment using the standard SQLite3 CLI. I created a standard relational table with 12 sample student records.

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

```

### Sample Dataset Rows

| First Name | Last Name | Age | Course |
| --- | --- | --- | --- |
| Kartik | Bhatia | 22 | Computer Science |
| Prashansa | Sharma | 21 | Electronics |
| Rahul | Joshi | 24 | Computer Science |
| Ananya | Das | 20 | Civil |

---

## Querying Database Metadata

Before jumping into the raw hex, I ran a few system queries to grab the core structural numbers directly from the engine:

```sql
PRAGMA page_size;
PRAGMA page_count;

SELECT name, rootpage FROM sqlite_master;

```

### Execution Output

* **Page Size**: 4096 bytes
* **Page Count**: 4 pages

### Internal Root Page Table

| Database Object | Root Page Number |
| --- | --- |
| `students` (Table) | 2 |
| `sqlite_autoindex_students_1` (Email Index) | 3 |
| `sqlite_autoindex_students_2` (PK Index) | 4 |

---

## Physical File Layout Map

Because my database configuration uses a strict 4096-byte ($0x1000$ hex bytes) page split, the physical file maps perfectly into four distinct memory windows:

| Page Number | Hex Address Range | What Lives Here? |
| --- | --- | --- |
| **Page 1** | `0x00000000 - 0x00000FFF` | Global Database Header + Master System Metadata (`sqlite_master`) |
| **Page 2** | `0x00001000 - 0x00001FFF` | `students` Table B-Tree Leaf Node (Actual Student Data) |
| **Page 3** | `0x00002000 - 0x00002FFF` | `UNIQUE` Email Index Leaf Node |
| **Page 4** | `0x00003000 - 0x00003FFF` | `PRIMARY KEY` Index Leaf Node |

---

## Main Database Header Breakdown

Running `xxd -g 1 -l 128 students.db` pulls the first 128 bytes of the file. The first line looks like this:

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00  SQLite format 3.

```

Decoding those hex pairs to ASCII reveals the string: `SQLite format 3\0`. This is the mandatory "magic string" signature used to authenticate that this file is an uncorrupted SQLite3 container.

### Core Header Field Extractions

| Target Bytes | Raw Hex | Decoded Value / Meaning |
| --- | --- | --- |
| **Bytes 0 - 15** | `53 51 ... 33 00` | `"SQLite format 3\0"` Magic Identifier string |
| **Bytes 16 - 17** | `10 00` | `0x1000` = **4096 bytes**. Confirms the exact page size. |

---

## Page 2 Analysis (The `students` Table Leaf Node)

Since Page 2 is the root page for our actual data records, I pulled its first 512 bytes using:

```bash
xxd -g 1 -s 4096 -l 512 students.db

```

### The 8-Byte Leaf Page Header

The page starts right at offset `0x1000` with these exact bytes:

```text
0d 00 00 00 0c 0c 75 00

```

Let's break down what each of these bytes tells us about the page state:

* `0d`: **Page Type Flag.** In SQLite, `0x0D` means this is a *Table B-Tree Leaf Page*. This confirms it contains actual row data records rather than internal node router keys.
* `00 00`: **First Freeblock Offset.** A value of 0 means there are no deleted or fragmented holes on this page.
* `00 0c`: **Cell Counter.** `0x000C` in hex means there are exactly **12 cells** (rows) stored on this page.
* `0c 75`: **Start of Cell Content Area.** `0x0C75` tells us where the unallocated free space ends and the data records begin. Relative to the page start, our data blocks start exactly at file location `0x1000 + 0x0C75 = 0x1C75`.
* `00`: **Fragmented Free Bytes.** 0 indicates clean allocation without fragments.

### The Cell Pointer Array

Immediately following the 8-byte header at offset `0x1008`, SQLite lists the cell pointer array. It consists of 2-byte offsets indicating where each row's data payload begins on the page:

```text
0f b4   0f 67   0f 1e   0e cf   0e 8b   0e 3d   0d e8   0d 9c   0d 4d   0d 02   0c b7   0c 75

```

### Parsing an Example Pointer

Let's track down the first pointer in the list: `0f b4`.

1. Convert `0x0FB4` hex to decimal $\rightarrow$ **4020**.
2. Add this to the Page 2 starting boundary ($4096$) $\rightarrow$ $4096 + 4020 = \mathbf{8116}$ (`0x1FB4`).
3. If we look at absolute address `0x1FB4` in the file, that's exactly where the first student record's binary body sits.

### How a Page Grows (Bidirectional Design)

An elegant detail here is that the page grows from both ends toward the middle to minimize memory shuffling:

```text
+---------------------------------------------------+ [Offset 0x1000]
| B-Tree Page Header (8 Bytes)                      |
+---------------------------------------------------+
| Cell Pointer Array                                |
| (Grows DOWNWARD: Row 0, Row 1, Row 2...)          |
+---------------------------------------------------+
|                                                   |
|                UNALLOCATED FREE SPACE             |
|                                                   |
+---------------------------------------------------+
| ...Cell Payloads (Row 2, Row 1, Row 0)            |
| (Grows UPWARD toward the top header)              |
+---------------------------------------------------+ [Offset 0x1FFF]

```

---

## Record Payload Breakdown

The row payload block starts climbing up from offset `0x0C75`. I inspected it using:

```bash
xxd -g 1 -s 7285 -l 512 students.db

```

Every record cell uses a standard internal structural wrapper layout:


$$\text{Cell} = \text{Payload Size (Varint)} + \text{RowID (Varint)} + \text{Header Size (Varint)} + \text{Serial Types} + \text{Raw Data Fields}$$

Looking at the text fields printed on the ASCII side of the dump, we can clearly isolate how different data types are handled:

### 1. String Extractions (`first_name`)

* **Hex Pairs**: `41 6e 61 6e 79 61`
* **ASCII Translation**: `Ananya`

### 2. Email Identification Field (`email`)

* **Hex Pairs**: `61 6e 61 6e 79 61 2e 64 61 73 40 65 78 61 6d 70 6c 65 2e 63 6f 6d`
* **ASCII Translation**: `ananya.das@example.com`

### 3. Course Field Extraction (`course`)

* **Hex Pairs**: `43 69 76 69 6c`
* **ASCII Translation**: `Civil`

### 4. Timestamp Generation Tracking (`created_at`)

* **Hex Pairs**: `32 30 32 36 2d 30 35 2d 31 33 20 32 31 3a 32 37 3a 31 31`
* **ASCII Translation**: `2026-05-13 21:27:11`

> **Key Finding**: This shows that by default, SQLite treats dates and timestamps as regular ASCII strings (TEXT) on disk, instead of compressing them into UNIX integer epochs.

---

## Analysis of `sqlite_master` Metadata

Page 1 acts as the control panel for the whole database file. Looking at its text dump, we can spot strings like `tablestudents` and the actual `CREATE TABLE students...` definition string.

This proves that SQLite explicitly writes your original raw DDL text directly onto the disk block, parsing it on startup to reconstruct the database's schema layout in memory.

---

## Index B-Tree Mechanics (Pages 3 & 4)

Pages 3 and 4 handle our lookups and open with the hex byte `0a`.

* `0a`: **Leaf Index B-Tree Page Flag.**

### Comparing Table Pages vs. Index Pages

| Structural Feature | Table B-Tree Page (`0x0d`) | Index B-Tree Page (`0x0a`) |
| --- | --- | --- |
| **Contains full row items?** | Yes | No |
| **Key definition** | The key *is* the implicit RowID. | The key is the actual sorted column data. |
| **Payload data** | Full database records. | Merely a pointer back to the target RowID. |

---

## Step-by-Step Lookup Pipeline Simulation

When executing a quick lookup statement:

```sql
SELECT * FROM students WHERE email='rahul.joshi@example.com';

```

The database engine traverses our storage pages using the following path:

```
[ Step 1: Read Page 1 Master Schema ]
  -> Verifies table names and notes that the "email" index resides on Page 3.
       v
[ Step 2: Query Index B-Tree Leaf Node ]
  -> Jumps directly to Page 3 (Offset 0x2000).
  -> Performs a quick binary search over the sorted strings until it finds 'rahul.joshi@example.com'.
       v
[ Step 3: Extract Targeted Reference Key ]
  -> Reads the companion value associated with that email string, which yields RowID = 3.
       v
[ Step 4: Jump directly to Data Page ]
  -> Routes straight to Page 2 (Offset 0x1000).
  -> Looks up index entry #3 in the cell pointer array to instantly locate offset 0x0D02.
       v
[ Step 5: Extract Row Data ]
  -> Reads absolute address 0x1D02 to extract Rahul's complete student record.

```

---

## Takeaways

1. **Pure B-Tree Storage Architecture**: Everything in SQLite is a B-Tree. Metadata, secondary indices, and physical record lines are all housed inside variations of internal B-Tree pages.
2. **Space-Saving Varints**: SQLite heavily relies on variable-length integers (Varints) to scale size. Small integers or short strings consume only single bytes instead of taking up large, static 32-bit blocks.
3. **Smart Page Budgeting**: The bidirectional layout (pointers growing forward, payloads growing backward) optimizes memory operations and minimizes shifting overhead when handling live changes on the disk.