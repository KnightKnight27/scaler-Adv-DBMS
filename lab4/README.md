# SQLite 3 Hex Dump Analysis

This lab explores the physical file structure of a SQLite 3 database by analyzing its hex dump. The primary focus is on understanding the B-tree implementation, page structure, and how pointers facilitate navigation between nodes.

## Setup & Database Generation

We created a test database (`my_database.db`) with a `users` table:
```sql
CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, description TEXT);
```

To demonstrate B-tree navigation, we need an **Interior Table B-Tree Node** (a page that points to other pages). Since SQLite uses a 4096-byte default page size, we inserted 20 rows containing ~1000 bytes of data each. This forced the root page to split and create an interior node that points to multiple leaf nodes.

The hex dump of this database is generated using `xxd` and is attached in this folder as `hexdump.txt`.

---

## 1. Addresses and Offsets

In SQLite, all data is divided into **Pages** (default 4096 bytes or `0x1000` bytes). 
- Pages are 1-indexed. Page 1 contains the 100-byte database header and the root of the `sqlite_schema` table.
- To find the byte offset of any page in the file, use the formula: `Offset = (Page_Number - 1) * Page_Size`.
  - **Page 2** starts at `(2 - 1) * 4096 = 4096 = 0x1000`.
  - **Page 3** starts at `(3 - 1) * 4096 = 8192 = 0x2000`.

Inside a page, addresses are stored as 2-byte offsets **relative to the start of the page**.

---

## 2. Navigation of Fields in the Page

Let's examine **Page 2**, which serves as the root interior B-tree node for our `users` table.
Here is the first 16 bytes of Page 2 at offset `0x1000`:

```text
00001000: 0500 0000 040f ec00 0000 0007 0ffb 0ff6  ................
```

### The Page Header
B-tree pages have a header (8 bytes for leaf, 12 bytes for interior):
- `0x05` (1 byte): **Page Flag**. `0x05` means this is an **Interior Table B-Tree Page**. (`0x0d` would mean a Leaf Page).
- `0x00 00` (2 bytes): **First freeblock** offset (0 means none).
- `0x00 04` (2 bytes): **Number of cells** on this page (There are 4 cells).
- `0x0f ec` (2 bytes): **Start of cell content area**. The actual data for the cells starts at offset `0x0fec` (4076 bytes into the page).
- `0x00` (1 byte): Number of fragmented free bytes.
- `0x00 00 00 07` (4 bytes): **Right-most pointer**. This points to Page 7, which holds the rows with the highest keys (rowids).

### The Cell Pointer Array
Immediately following the header is the **Cell Pointer Array**. It contains 2-byte offsets for each cell, indicating where they are located in the page.
Looking at the bytes `0ffb 0ff6 0ff1 0fec`:
- Cell 0 is at offset `0x0ffb`
- Cell 1 is at offset `0x0ff6`
- Cell 2 is at offset `0x0ff1`
- Cell 3 is at offset `0x0fec`

Cells in SQLite are populated from the **end** of the page backwards, which is why the start of the cell content area is near the end of the 4096-byte page.

---

## 3. B-Tree Nodes and Lookup

### Interior B-Tree Structure (Pointers)

Let's look at the actual cells (pointers) in the interior node (Page 2).
We check offset `0x1000 + 0x0fec = 0x1fec` (the cell content area).

```text
00001fea: 00 00 00 06 10 00 00 00 05 0c 00 00 00 04  ................
00001ffa: 08 00 00 00 03 04                          ......
```

Realigning to our offsets from the pointer array:
1. **Cell 0 (`0x0ffb`)**: `00 00 00 03 04`
   - `00 00 00 03`: Left child pointer. Points to **Page 3**.
   - `04`: Integer key. This means Page 3 contains rows with `id <= 4`.
2. **Cell 1 (`0x0ff6`)**: `00 00 00 04 08`
   - Left child: **Page 4**. Max key: `8`.
3. **Cell 2 (`0x0ff1`)**: `00 00 00 05 0c`
   - Left child: **Page 5**. Max key: `0x0c` (12).
4. **Cell 3 (`0x0fec`)**: `00 00 00 06 10`
   - Left child: **Page 6**. Max key: `0x10` (16).

And the **Right-most pointer** from the header is `0x00 00 00 07` (**Page 7**), which catches all keys `> 16`.

### Traversing a Lookup

Imagine we execute: `SELECT * FROM users WHERE id = 10;`
1. SQLite checks the schema and finds the `users` table root page is **Page 2**.
2. It reads the header of Page 2, determining it's an interior node (`0x05`).
3. It scans the cells:
   - Cell 0 (max key 4) -> 10 > 4 (Skip)
   - Cell 1 (max key 8) -> 10 > 8 (Skip)
   - Cell 2 (max key 12) -> 10 <= 12 (**Match!**)
4. SQLite follows the pointer to **Page 5** (`0x00 00 00 05`).
5. It calculates the file offset for Page 5: `(5 - 1) * 4096 = 16384 = 0x4000`.
6. At offset `0x4000`, it finds a Leaf Node (`0x0d`), reads its cell pointer array, and locates the exact payload for `id = 10`.

---

## 4. Leaf Node Structure (The Payload)

Let's look at **Page 3** (offset `0x2000`), which holds rows 1 through 4.
Header:
```text
00002000: 0d00 0000 0400 2800 0c0a ...
```
- `0x0d`: **Leaf Table B-Tree Page**.

Let's examine the first cell payload located at offset `0x2c0a` (inside Page 3):
```text
00002c0a: 8773 0105 0019 8f5d 5573 6572 2031 3937  .s.....]User 197
```
- **`87 73`** (varint): Payload Size. Decodes to 1011 bytes.
- **`01`** (varint): Rowid. This is `id = 1`.
- **`05`** (varint): Header size. The record header is 5 bytes long.
- **`00`**: Column 0 (id). Since it's aliased to the rowid, it takes 0 bytes in the payload.
- **`19`**: Column 1 (name). Decodes to a string of length 6.
- **`8f 5d`**: Column 2 (description). Decodes to a string of length 1000.
- **Payload Data**: `55 73 65 72 20 31` translates to `"User 1"`, immediately followed by the 1000 hex characters.
