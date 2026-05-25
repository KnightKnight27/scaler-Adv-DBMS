# SQLite3 Database Internal Structure Analysis using XXD

## Objective

This lab focuses on analyzing the physical structure and internal storage engine of a SQLite3 database. Using a customized `products` database, we generate hexadecimal dumps using the `xxd` command line tool and decode the raw bytes to understand the low-level representation of:
* Database file headers
* B-tree leaf page formats
* Variable-length integer representations (varints)
* Page headers and cell pointer arrays
* Record payload layout and serialization types
* Master schema definitions (`sqlite_master`)

---

## Database Definition and Records

The analysis was performed on a custom `products` database created with the following SQL schema and records:

### Table Schema
```sql
CREATE TABLE products (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    category TEXT NOT NULL
);
```

### Table Data
Five records were inserted into the `products` table:

| id | name | category |
| -- | ---- | -------- |
| 1  | Laptop | Electronics |
| 2  | Coffee Maker | Appliances |
| 3  | Desk Chair | Furniture |
| 4  | Running Shoes | Apparel |
| 5  | Notebook | Stationery |

---

## Database Metadata

Using SQLite database query PRAGMAs, the metadata properties of the database file were retrieved:

```sql
PRAGMA page_size;
PRAGMA page_count;
SELECT name, rootpage FROM sqlite_master;
```

**Output:**
* **Page Size:** 4096 bytes
* **Page Count:** 2 pages
* **Root Pages:**
  * `products` table root page: **Page 2**
  * `sqlite_master` root page: **Page 1** (default page 1)

Since each page has a size of 4096 bytes (0x1000 in hex), the physical layouts are split at the following offsets:
* **Page 1 (Metadata / sqlite_master):** `0x0000` to `0x0FFF`
* **Page 2 (Products B-Tree Leaf Page):** `0x1000` to `0x1FFF`

---

## File Header Analysis (Page 1)

We inspect the first 100 bytes of the database file using:
```bash
xxd -g 1 -l 100 products.db
```

### Raw Bytes
```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 10 00 01 01 0c 40 20 20 00 00 00 02 00 00 00 02
```

### Header Breakdown
* **Bytes 0–15 (`53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00`):**
  This corresponds to the string `"SQLite format 3\0"`. This is the signature/magic number identifying the file format.
* **Bytes 16–17 (`10 00`):**
  Page size in bytes. `0x1000` converts to `4096` in decimal, matching our metadata `page_size` value.
* **Bytes 24–27 (`00 00 00 02`):**
  File change counter, representing database modifications.
* **Bytes 28–31 (`00 00 00 02`):**
  Total page count of the database (2 pages).

---

## B-Tree Leaf Page Parsing (Page 2)

Page 2 contains the actual record data for the `products` table. The head of Page 2 was dumped using:
```bash
xxd -g 1 -s 4096 -l 32 products.db
```

### Raw Bytes
```text
00001000: 0d 00 00 00 05 0f 76 00 0f dd 0f c1 0f a8 0f 8e
00001010: 0f 76 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

### Decoding the B-Tree Leaf Page Header (8 Bytes)
The B-tree leaf table page header spans from offset `0x1000` to `0x1007` and decodes as follows:

| Bytes | Decoded Value (Decimal) | Meaning |
| ----- | ----------------------- | ------- |
| `0d` | 13 | Page Type: Leaf Table B-Tree Page (value 0x0D / 13) |
| `00 00` | 0 | First Freeblock offset (0 indicates no freeblocks) |
| `00 05` | 5 | Number of cells (records) stored in this page |
| `0f 76` | 3958 | Start of the cell content area (relative offset from page start) |
| `00` | 0 | Fragmented free bytes count |

### Cell Pointer Array
Directly following the page header (from offset `0x1008`) is the Cell Pointer Array, containing `5` two-byte offsets pointing to the start of each record:
1. **Cell 0 Pointer:** `0f dd` (offset `4061` relative to page start -> file address `4096 + 4061 = 8157`)
2. **Cell 1 Pointer:** `0f c1` (offset `4033` relative to page start -> file address `4096 + 4033 = 8129`)
3. **Cell 2 Pointer:** `0f a8` (offset `4008` relative to page start -> file address `4096 + 4008 = 8104`)
4. **Cell 3 Pointer:** `0f 8e` (offset `3982` relative to page start -> file address `4096 + 3982 = 8078`)
5. **Cell 4 Pointer:** `0f 76` (offset `3958` relative to page start -> file address `4096 + 3958 = 8054`)

SQLite pages grow in opposite directions: headers and pointer arrays grow downwards, while records are pushed upwards from the bottom of the page (`0x1FFF`). The space between the pointer array and cell contents is free space.

---

## Record Payload Analysis

Let's analyze two specific records from the cell content area. The records can be viewed via:
```bash
xxd -g 1 -s 8054 -l 200 products.db
```

### Record 1 (Laptop)
Found at pointer `0x0FDD` (absolute address `8157` / hex `0x1FDD`):
* **Raw Hex Dump:**
  ```text
  00001fd5:                   15 01 04 00 19 23 4c 61 70 74 6f 70 45 6c 65 63 74 72 6f 6e 69 63 73
  ```
* **Payload Decoding:**
  * **Payload Size (`15`):** 21 bytes (varint).
  * **Row ID / Key (`01`):** Row ID `1` (varint).
  * **Record Header Size (`04`):** The record header is 4 bytes long (varint).
  * **Serial Types (Within Header):**
    * **`00`:** Row ID is integer primary key (value is stored in the Row ID varint, so it doesn't need payload bytes).
    * **`19`:** String of length `(25 - 12) / 2 = 6` bytes (representing `"Laptop"`).
    * **`23`:** String of length `(35 - 12) / 2 = 11` bytes (representing `"Electronics"`).
  * **Column Data:**
    * **`4c 61 70 74 6f 70`:** ASCII `"Laptop"`
    * **`45 6c 65 63 74 72 6f 6e 69 63 73`:** ASCII `"Electronics"`

### Record 5 (Notebook)
Found at pointer `0x0F76` (absolute address `8054` / hex `0x1F76`):
* **Raw Hex Dump:**
  ```text
  00001f76: 16 05 04 00 1d 21 4e 6f 74 65 62 6f 6f 6b 53 74
  00001f86: 61 74 69 6f 6e 65 72 79
  ```
* **Payload Decoding:**
  * **Payload Size (`16`):** 22 bytes.
  * **Row ID / Key (`05`):** Row ID `5`.
  * **Record Header Size (`04`):** 4 bytes.
  * **Serial Types:**
    * **`00`:** Primary Key alias.
    * **`1d`:** String of length `(29 - 12) / 2 = 8` bytes (representing `"Notebook"`).
    * **`21`:** String of length `(33 - 12) / 2 = 10` bytes (representing `"Stationery"`).
  * **Column Data:**
    * **`4e 6f 74 65 62 6f 6f 6b`:** ASCII `"Notebook"`
    * **`53 74 61 74 69 6f 6e 65 72 79`:** ASCII `"Stationery"`

---

## Schema Storage and sqlite_master

The SQLite master schema table (`sqlite_master`) resides on **Page 1**. We can extract the schema strings using the standard `strings` command:
```bash
strings products.db
```
**Output:**
```text
CREATE TABLE products (id INTEGER PRIMARY KEY, name TEXT NOT NULL, category TEXT NOT NULL)
```
This confirms that SQLite stores the DDL SQL schema commands directly inside page 1, allowing it to reconstruct the structure of the database tables at runtime.

---

## Key Takeaways

1. **Compact Internal Storage:** Integer primary keys are stored as Row IDs in B-tree cells, avoiding storage redundancy.
2. **Variable-Length Encoding:** Varints are widely used for offsets and lengths to save disk space.
3. **Structured B-Trees:** SQLite relies on double-ended pages (headers at top growing down, content at bottom growing up) to keep insertions efficient and memory-aligned.
4. **Self-Contained Schema:** The table schema is embedded directly within the database metadata page, ensuring data portability.
