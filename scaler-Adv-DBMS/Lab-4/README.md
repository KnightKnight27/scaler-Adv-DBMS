# Lab 4: Under the Hood of SQLite 3 Storage & B-Tree Navigation

> **DBMS Coursework**  
> **Student:** Nandani Kumari (24bcs10317)  

---

## 1. Overview of Lab Deliverables

This lab is a hand-on investigation into how SQLite 3 registers table data and indexes on disk. By examining a formatted hexadecimal print of a database file, we map out the physical offsets, parse B-tree interior routing cells, and decode the binary payload headers of individual leaf rows.

The following files constitute this lab module:

| Deliverable | Purpose |
| :--- | :--- |
| `my_database.db` | The binary SQLite 3 database file (28 KB) containing the test schema. |
| `hexdump.txt` | The formatted ASCII-hex dump of the database file. |
| `README.md` | This analysis report detailing the offsets, page layouts, and traversal paths. |

---

## 2. Database Construction & Data Population

To study B-tree node splits, we designed a simple table structure in SQLite:

```sql
CREATE TABLE users (
    id          INTEGER PRIMARY KEY,
    name        TEXT,
    description TEXT
);
```

### Inducing a Split
SQLite's default database page size is **4096 bytes** (`0x1000`). We wanted to force a root page overflow so that the root page would split into an **Interior Table B-Tree Node** pointing to multiple child pages. 

To achieve this, we populated the `users` table with **20 rows**, filling each row's description field with a 1000-character alphanumeric string. This configuration quickly exceeded the page capacity of a single page and forced SQLite to split the data across pages 3 through 7, transforming Page 2 into the interior coordinator.

The hex print was compiled with a custom python binary dumper:
```bash
# Output hexadecimal database image
python -c "..." > hexdump.txt
```

---

## 3. File Layout & Page Offset Calculations

In SQLite, the storage space is segmented into fixed-size **Pages** of `4096` bytes.

* The file uses a **1-based index** for page references.
* Page 1 is reserved for the general 100-byte file header and the system catalog schema (`sqlite_schema`).
* To convert a 1-based Page Number $P$ to its byte offset $O$ in the file, we use:
  $$O = (P - 1) \times 4096$$

| Page ID | File Offset (Decimal) | File Offset (Hex) |
| :---: | :---: | :---: |
| **Page 1** | `0` | `0x0000` |
| **Page 2** | `4096` | `0x1000` |
| **Page 3** | `8192` | `0x2000` |
| **Page 4** | `12288` | `0x3000` |
| **Page 5** | `16384` | `0x4000` |

Offsets within any individual page are represented using compact 2-byte values.

---

## 4. Inside the Page: Page Header & Pointers

Let's study **Page 2** (file offset `0x1000`), which is the interior node driving the index path of the `users` table. The first 16 bytes of Page 2 read:

```text
00001000: 0500 0000 040f ec00 0000 0007 0ffb 0ff6  ................
```

### The 12-Byte Header Structure
Interior table nodes feature a 12-byte header, containing the page type flag, cell counts, content offsets, and a rightmost pointer.

| Bytes | Value | Field Description |
| :--- | :---: | :--- |
| `0x00` | `05` | **Page Flag:** `0x05` indicates an Interior Table B-Tree Page (`0x0d` would mean a Leaf Page). |
| `0x01 - 0x02` | `00 00` | Byte offset of the first freeblock within the page (`0` means none). |
| `0x03 - 0x04` | `00 04` | **Cell Count:** This page contains exactly 4 index key cells. |
| `0x05 - 0x06` | `0f ec` | **Content Start:** Cells begin at offset `4076` (`0x0fec`) inside this page. |
| `0x07` | `00` | Count of fragmented free bytes. |
| `0x08 - 0x0b` | `00 00 00 07` | **Rightmost Pointer:** Points to Page 7, which handles keys higher than `16`. |

### The Cell Pointer Array
The cell pointer list starts at byte 12 (immediately after the header) and consists of 2-byte offsets indicating the position of each cell.

Tracing the values `0ffb 0ff6 0ff1 0fec`:

| Cell Number | Pointer Value | Absolute Offset in File |
| :---: | :---: | :---: |
| **Cell 0** | `0x0ffb` | `0x1ffb` |
| **Cell 1** | `0x0ff6` | `0x1ff6` |
| **Cell 2** | `0x0ff1` | `0x1ff1` |
| **Cell 3** | `0x0fec` | `0x1fec` |

> **Space Strategy:** The B-tree engine employs a dual-growth pattern. Pointers grow downwards from the top of the page, whereas the actual cells are added upwards from the end of the page.

---

## 5. Interior Node Structures & Search Traversal

### Cell Layout in Interior Pages
Looking at the data starting at offset `0x1fec` in our hex dump:

```text
00001ff0: 1000 0000 050c 0000 0004 0800 0000 0304  ................
```

Decoding these bytes using the offsets from the pointer array gives the following structure:

| Index | Offset | Raw Cell Bytes | Left Child Link | Upper Key Boundary |
| :---: | :---: | :--- | :---: | :---: |
| **Cell 0** | `0x0ffb` | `00 00 00 03 04` | **Page 3** | Keys $\le 4$ |
| **Cell 1** | `0x0ff6` | `00 00 00 04 08` | **Page 4** | Keys $\le 8$ |
| **Cell 2** | `0x0ff1` | `00 00 00 05 0c` | **Page 5** | Keys $\le 12$ |
| **Cell 3** | `0x0fec` | `00 00 00 06 10` | **Page 6** | Keys $\le 16$ |

The rightmost pointer `0x00 00 00 07` routes keys greater than `16` to **Page 7**.

### Index Search Workflow
Suppose we perform a lookup query:
```sql
SELECT * FROM users WHERE id = 10;
```

1. **Root Access:** The database engine retrieves Page 2 as the root node of the table.
2. **Page Classify:** Checking the page flag `0x05` tells the parser that this is an interior index node.
3. **Linear Key Search:** The search key `10` is tested against the cell boundaries:
   * Cell 0 (Max 4): `10 > 4` $\rightarrow$ Skip.
   * Cell 1 (Max 8): `10 > 8` $\rightarrow$ Skip.
   * Cell 2 (Max 12): `10 <= 12` $\rightarrow$ **Match!**
4. **Descent:** The search logic follows the left-child pointer of Cell 2, pointing to **Page 5**.
5. **Address Resolve:** The page's start location is calculated: $(5 - 1) \times 4096 = 16384$ (`0x4000`).
6. **Leaf Fetch:** The engine loads Page 5, reads the records, and retrieves the row containing `id = 10`.

---

## 6. Leaf Page Layout & Record Decoding

Let's study **Page 3** (offset `0x2000`), which holds records with keys `1` to `4`.

### Page 3 Header Info
```text
00002000: 0d00 0000 0400 2800 ...
```
* `0x0d` at byte 0 confirms that this is a **Leaf Table B-Tree Node**.
* `0x00 04` indicates the page contains exactly 4 cells (records).
* `0x00 28` states that the data records begin at offset `40` (`0x0028`) in the page.

### Row 1 Cell Extraction
According to the cell pointer array, Cell 0 is located at page offset `0x0c0a`. Inspecting the hex values from this location:

```text
00002c0a: 8773 0105 0019 8f5d 5573 6572 2031 4235  .s.....]User 1B5
```

Here is how the bytes are decoded:

| Byte Group | Header Field | Decoded Value | Purpose & Meaning |
| :--- | :--- | :--- | :--- |
| `87 73` | **Payload Size** | `1011` bytes | A varint defining the size of the record payload. |
| `01` | **Row ID** | `1` | A varint defining the record key. |
| `05` | **Header Size** | `5` bytes | A varint indicating where the column-type header ends. |
| `00` | **Col 0 Type** | `0` | The primary key `id` is mapped directly to the Row ID, requiring 0 payload bytes. |
| `19` | **Col 1 Type** | `25` | A string data type of length: $\frac{25 - 13}{2} = 6$ bytes (stores `"User 1"`). |
| `8f 5d` | **Col 2 Type** | `2013` | A varint representing a string of length: $\frac{2013 - 13}{2} = 1000$ bytes (stores the alphanumeric description). |
| `55 73 65 72 20 31` | **Record Data** | `"User 1"` | The ASCII representation of the user name, followed by the description character sequence. |

---

## 7. Summary of Core DBMS Concepts

1. **Page Partitioning:** Database storage is structured into fixed-sized chunks called pages. Relations are organized as B-Trees mapped over these page blocks.
2. **Opposing Page growth:** Metadata (headers, cell pointers) is saved from the top of the page downwards, while row data is saved from the bottom upwards.
3. **Index vs Data Pages:** Leaf pages store actual record contents. Interior pages function strictly as routing tables, holding key boundaries and sub-page pointers.
4. **Compact Serialization:** Compact variable-length integers (varints) and serial-type record headers minimize disk storage footprints.

---

> *Submitted as DBMS Laboratory Assignment 4 coursework.*
