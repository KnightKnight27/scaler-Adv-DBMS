# Lab 4 — SQLite 3 Physical Storage & B-Tree Structure Analysis

> **Course:** Advanced Database Management Systems  
> **Author:** Ayush Kumar Patra (24bcs10474)  

---

## Abstract

This laboratory assignment conducts a deep-dive analysis of the **physical disk representation** of a SQLite 3 database file. By inspecting its raw binary footprint via hex dumps, we study the implementation details of SQLite's **B-tree architecture**, **page-level structures**, **varint encodings**, and how record payloads are mapped to disk pages.

---

## 1. Directory Structure & Files

The lab deliverables are composed of the following key artifacts:

| Filename | Purpose / Description |
| :--- | :--- |
| `my_database.db` | The actual target SQLite 3 binary database file (28 KB). |
| `hexdump.txt` | A formatted ASCII hexadecimal dump of `my_database.db` showing raw page bytes. |
| `README.md` | This technical walkthrough and structural walkthrough of the database dump. |

---

## 2. Environment Setup & Data Insertion

To begin, we created a relational schema containing a single table named `users`:

```sql
CREATE TABLE users (
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    description TEXT
);
```

### Forcing page splits
Under SQLite's default settings, data pages have a fixed size of **4096 bytes** (`0x1000`). We wanted to inspect an **Interior Table B-Tree Node** (a directory node pointing to multiple leaf child pages). To trigger a split on the root node, we inserted **20 records**, each containing a descriptive string payload of exactly 1000 characters. 

The large payload size quickly overflowed the root page's capacity, forcing the database engine to allocate new leaf nodes (Pages 3 through 7) and convert Page 2 into an interior routing page.

The hex dump file was generated using a custom hexadecimal translation utility:
```bash
# Formatted hexadecimal conversion
python -c "import os; ..." > hexdump.txt
```

---

## 3. Addressing & Disk Layout

SQLite structures its databases as a sequence of fixed-size blocks called **Pages**. 

* **Indexing:** Pages are indexed starting at 1.
* **Page 1 Layout:** The first page is unique; it begins with a standard 100-byte database header and houses the schema definition table (`sqlite_schema`).
* **Byte Offset Formula:** The absolute offset in bytes from the start of the file to any page $N$ is calculated as:
  $$\text{Offset} = (N - 1) \times \text{Page Size}$$

| Page Number | Start Offset (Decimal) | Start Offset (Hexadecimal) |
| :---: | :--- | :--- |
| **Page 1** | `0` | `0x0000` |
| **Page 2** | `4096` | `0x1000` |
| **Page 3** | `8192` | `0x2000` |
| **Page 4** | `12288` | `0x3000` |
| **Page 5** | `16384` | `0x4000` |

Internally, pointers and offsets referencing positions within a page are represented using 2-byte unsigned integers.

---

## 4. Header Anatomy & Page Navigation

Let's dissect **Page 2**, which acts as the root index node for the `users` table. The first 16 bytes of Page 2 at file offset `0x1000` appear as follows:

```text
00001000: 0500 0000 040f ec00 0000 0007 0ffb 0ff6  ................
```

### The 12-Byte Interior Page Header
SQLite B-tree pages start with a specialized header structure. For leaf pages, this header spans 8 bytes, while interior nodes use 12 bytes to accommodate a rightmost child pointer.

| Byte Range | Hex Value | Field Meaning |
| :--- | :--- | :--- |
| `0x00` | `05` | **Page Type Flag** (`0x05` indicates an Interior Table B-Tree Node; `0x0d` would indicate a Leaf Node) |
| `0x01 - 0x02` | `00 00` | Start address of the first freeblock within this page (`0` = no free space) |
| `0x03 - 0x04` | `00 04` | **Cell Count** (the page manages exactly 4 keys pointing to sub-pages) |
| `0x05 - 0x06` | `0f ec` | **Cell Content Start Offset** (cells grow upward from byte 4076 in the page) |
| `0x07` | `00` | Fragmented free bytes count |
| `0x08 - 0x0b` | `00 00 00 07` | **Right-most Child Pointer** (points to Page 7, storing values higher than the largest key) |

### The Cell Pointer Array
Directly following the page header is the **Cell Pointer Array**. This array lists 2-byte offsets indicating where each key/pointer cell is stored. 

Inspecting the subsequent bytes (`0ffb 0ff6 0ff1 0fec`):

| Index | Relative Offset | Absolute Offset |
| :---: | :--- | :--- |
| **Cell 0** | `0x0ffb` | `0x1ffb` |
| **Cell 1** | `0x0ff6` | `0x1ff6` |
| **Cell 2** | `0x0ff1` | `0x1ff1` |
| **Cell 3** | `0x0fec` | `0x1fec` |

> **Allocation Strategy:** SQLite optimizes page storage by using a split-growth model: the header and pointer array grow sequentially downward from the top of the page, while the actual record cells grow upward from the bottom.

---

## 5. B-Tree Traversal & Node Lookups

### Interior Node Data Layout
The cells of Page 2 are located in the region starting at file offset `0x1fec`. Let's inspect the hex values around `0x1fe0` to `0x2000`:

```text
00001ff0: 1000 0000 050c 0000 0004 0800 0000 0304  ................
```

By tracing the pointer array offsets, we unpack the specific structures of these 4 interior routing cells:

| Cell Index | Target Offset | Byte Representation | Left Child Pointer | Maximum Key Boundary |
| :---: | :--- | :--- | :---: | :--- |
| **Cell 0** | `0x0ffb` | `00 00 00 03 04` | **Page 3** | Keys $\le 4$ |
| **Cell 1** | `0x0ff6` | `00 00 00 04 08` | **Page 4** | Keys $\le 8$ |
| **Cell 2** | `0x0ff1` | `00 00 00 05 0c` | **Page 5** | Keys $\le 12$ |
| **Cell 3** | `0x0fec` | `00 00 00 06 10` | **Page 6** | Keys $\le 16$ |

Any keys greater than `16` fallback to the rightmost child pointer: **Page 7**.

### Traversal Demonstration
Suppose we execute the following database query:
```sql
SELECT * FROM users WHERE id = 10;
```

1. **Root Read:** The engine queries `sqlite_schema`, identifying **Page 2** as the root for `users`.
2. **Page Evaluation:** Reading `0x05` at the start of Page 2 identifies it as an interior index node.
3. **Linear Scan:** The engine compares search key `10` against each cell boundary:
   * Cell 0 (Max 4): `10 > 4` $\rightarrow$ Skip.
   * Cell 1 (Max 8): `10 > 8` $\rightarrow$ Skip.
   * Cell 2 (Max 12): `10 <= 12` $\rightarrow$ **Match Found!**
4. **Pointer Redirection:** The engine follows Cell 2's child pointer pointing to **Page 5**.
5. **Disk Offset Lookup:** The page offset is computed: $(5 - 1) \times 4096 = 16384$ (`0x4000`).
6. **Leaf Inspection:** Page 5 is read (Page Flag `0x0d` $\rightarrow$ Leaf). The record containing `id = 10` is parsed from the payload area.

---

## 6. Leaf Node Analysis & Record Payloads

Now let's examine **Page 3** (offset `0x2000`), which stores the records with primary keys `1` through `4`.

### Page 3 Header
```text
00002000: 0d00 0000 0400 2800 0c0a ...
```
* `0x0d` at byte 0 verifies that this is a **Leaf Table B-Tree Node**.
* `0x00 04` specifies that the page stores exactly 4 records.
* `0x00 28` indicates that the cell content area begins at offset `40` (`0x0028`).

### Deconstructing the First Cell Payload (Row 1)
The cell pointer array indicates Cell 0 is located at offset `0x0c0a`. Reading from this offset:

```text
00002c0a: 8773 0105 0019 8f5d 5573 6572 2031 3833  .s.....]User 183
```

Here is the structural field breakdown of the cell bytes:

| Byte Value | Field Name | Decoded Representation | Meaning / Rationale |
| :--- | :--- | :--- | :--- |
| `87 73` | **Payload Size** | `1011` bytes | Variable-length integer (varint) representing the total size of the record payload. |
| `01` | **Row ID** | `id = 1` | Varint key representing the primary key of this row. |
| `05` | **Header Size** | `5` bytes | Varint size marking the end of the record header. |
| `00` | **Column 0 Serial Type** | `0` | The primary key `id` is stored within the B-tree row ID, requiring 0 payload bytes. |
| `19` | **Column 1 Serial Type** | `25` | Represents a string. Length is computed as: $\frac{25 - 13}{2} = 6$ bytes (stores `"User 1"`). |
| `8f 5d` | **Column 2 Serial Type** | `2013` | Varint representing a string. Length is computed as: $\frac{2013 - 13}{2} = 1000$ bytes (stores the unique random description). |
| `55 73 65 72 20 31` | **Record Data** | `"User 1"` | The ASCII bytes of the user's name, immediately followed by the 1000-byte random description field. |

---

## 7. Crucial Architectural Takeaways

1. **Page-Based B-Tree Layout:** SQLite databases are stored in structured page blocks. Fast search traversal is enabled by mapping relationships using B-Trees across pages.
2. **Bidirectional Space Management:** Inside a page, structural metadata grows downward from the header (top-to-bottom), while data payloads accumulate upward from the bottom (bottom-to-top).
3. **Index/Data Separation:** Interior routing cells only carry (page pointer, key) boundaries to guide queries. They contain no actual row data, which is stored exclusively in Leaf nodes.
4. **Efficient Storage Representation:** SQLite leverages variable-length integers (varints) to pack numbers tightly, and embeds schema descriptions within record headers to optimize disk space.

---

> *Submitted as DBMS Laboratory Assignment 4 coursework.*
