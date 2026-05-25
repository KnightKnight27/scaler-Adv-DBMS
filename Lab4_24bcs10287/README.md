# Lab 4 - SQLite Hex Dump & B-Tree Analysis

**Author:** Abhiroop Sistu 
**Tools Used:** SQLite 3, `xxd` (Hex dumper)

---

## Overview

This lab demonstrates the physical on-disk format of a SQLite 3 database. By generating a custom database and dumping its raw hexadecimal contents, we can manually trace the database header, table schemas, and internal B-Tree page structures.

---

## File Directory

| File | Description |
| :--- | :--- |
| `setup_roster.sql` | The SQL script used to define the schema and populate the data. |
| `roster.db` | The live SQLite binary file generated via terminal. |
| `hexdump.txt` | The 16-column hex dump of the database generated using `xxd`. |
| `README.md` | This analysis document. |

---

## Reproduction Steps

To recreate the database and hex dump from scratch, run the following commands in your terminal:

```bash
sqlite3 roster.db < setup_roster.sql
xxd -g 1 -c 16 roster.db > hexdump.txt
```

You can verify the metadata inside SQLite using:

```bash
sqlite3 roster.db ".dbinfo"
```

---

# Hex Dump Analysis

## 1. Database Header (Page 1)

SQLite divides databases into fixed-size pages. By default, the page size is 4096 bytes (`0x1000` in hex). The first 100 bytes of the file (and Page 1) contain the global database header.

If you look at the top of `hexdump.txt` (Offset `00000000:`):

- **Bytes 0–15:** The magic string `SQLite format 3\0` identifies the file type.
- **Bytes 16–17:** `10 00` translates to `0x1000`, confirming the 4096-byte page size.

---

## 2. Reading the B-Tree Page Headers

Right after the 100-byte database header, the root B-Tree for the `sqlite_schema` table begins. Every subsequent page starts immediately with a B-Tree header.

The very first byte of a page dictates its type:

| Hex Value | Meaning |
| :--- | :--- |
| `0x0d` | Table Leaf B-Tree (stores actual row data) |
| `0x05` | Table Interior B-Tree (stores pointers to other pages) |
| `0x0a` | Index Leaf B-Tree (stores indexed keys) |

---

## 3. Navigating the `players` Table Data

By running:

```sql
SELECT rootpage FROM sqlite_schema;
```

we know our `players` table is located on **Page 2**.

To find Page 2 in the hex dump, we calculate the offset:

```text
(Page Number - 1) * Page Size
= (2 - 1) * 4096
= 4096
= 0x1000
```

Searching for `00001000:` in the hex dump reveals the start of Page 2:

```text
00001000: 0d 00 00 00 05 ...
```

### Decoding the Header

- `0d` → Confirms this is a **Table Leaf page** containing actual row data.
- `00 00` → Indicates there are no fragmented free blocks.
- `00 05` → Indicates there are 5 cells, matching the 5 inserted player records.

Following this header is the **Cell Pointer Array**, which tells SQLite exactly what byte offset to jump to in order to read the payload (the varint-encoded row data) located at the bottom of the page.

---

# Conclusion

By analyzing the raw hexadecimal output, we can observe that SQLite behaves exactly as a localized paging system. Data rows grow upwards from the bottom of the page, while the header and pointer arrays grow downward from the top. This structure allows SQLite to efficiently manage storage and perform fast `O(log n)` lookups without relying on an external database server architecture.