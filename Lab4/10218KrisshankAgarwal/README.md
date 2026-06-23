# 📚 Analyzing a SQLite3 Database File Using `xxd`

---

## Overview

This document demonstrates how to analyze the internal structure of a SQLite3 database file using the Linux command-line utility `xxd`.

### Database Information

| Property      | Value               |
| ------------- | ------------------- |
| Database Name | `my_database`       |
| Table Name    | `users`             |
| Page Size     | `4096 Bytes (4 KB)` |
| Total Pages   | `2`                 |

---

## Table Contents

The result of:

```sql
SELECT * FROM users;
```

| id | name   |
| -- | ------ |
| 1  | Alice  |
| 2  | Bob    |
| 3  | Robin  |
| 4  | Monica |
| 5  | Tushar |
| 6  | Samrat |

---

## Generating the Hex Dump

Command used:

```bash
xxd -g 1 -a my_database
```

---

# Raw Hex Dump

```text
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 10 00 01 01 00 40 20 20 00 00 00 04 00 00 00 02
00000020: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 04
00000030: 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00 00
00000040: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00000050: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 04
00000060: 00 2e 76 89 0d 00 00 00 01 0f b2 00 0f b2 00 00
...
00000fb0: 00 00 4c 01 06 17 17 17 01 79 74 61 62 6c 65 75
00000fc0: 73 65 72 73 75 73 65 72 73 02 43 52 45 41 54 45
00000fd0: 20 54 41 42 4c 45 20 75 73 65 72 73 28 69 64 20
00000fe0: 49 4e 54 45 47 45 52 20 50 52 49 4d 41 52 59 20
00000ff0: 4b 45 59 2c 20 6e 61 6d 65 20 54 45 58 54 20 29
00001000: 0d 00 00 00 06 0f c3 00 0f f6 0f ee 0f e4 0f d9
...
00001fc0: 00 00 00 09 06 03 00 19 53 61 6d 72 61 74 09 05
00001fd0: 03 00 19 54 75 73 68 61 72 09 04 03 00 19 4d 6f
00001fe0: 6e 69 63 61 08 03 03 00 17 52 6f 62 69 6e 06 02
00001ff0: 03 00 13 42 6f 62 08 01 03 00 17 41 6c 69 63 65
```

---

# SQLite3 File Architecture

The database consists of **two pages**, each having a size of **4096 bytes**.

## High-Level Page Layout

| Page   | Address Range             | Node Type       | Contents               |
| ------ | ------------------------- | --------------- | ---------------------- |
| Page 1 | `0x00000000 - 0x00000FFF` | Table Leaf Node | SQLite Header + Schema |
| Page 2 | `0x00001000 - 0x00001FFF` | Table Leaf Node | `users` Table Records  |

---

# Page 1: Database Header & Schema

---

## 1. SQLite Database Header

Located at offsets `0x00000000 - 0x00000063`.

### Header Bytes

```text
53 51 4C 69 74 65 20 66
6F 72 6D 61 74 20 33 00
```

### Header Interpretation

| Offset  | Hex Value           | Meaning                           |
| ------- | ------------------- | --------------------------------- |
| 0 - 15  | `SQLite format 3\0` | Magic Header String               |
| 16 - 17 | `10 00`             | Page Size = 4096 Bytes            |
| 21      | `40`                | Maximum Embedded Payload Fraction |
| 22      | `20`                | Minimum Embedded Payload Fraction |
| 28 - 31 | `00 00 00 02`       | Total Pages = 2                   |

---

## 2. B-Tree Page Header (Page 1)

Located immediately after the database header.

```text
00000060:
00 2e 76 89 0d 00 00 00 01 0f b2 00 ...
              ^^
```

### Structure

| Offset      | Value  | Description             |
| ----------- | ------ | ----------------------- |
| `0x64`      | `0D`   | Table Leaf Node         |
| `0x65-0x66` | `0000` | First Free Block        |
| `0x67-0x68` | `0001` | Number of Cells         |
| `0x69-0x6A` | `0FB2` | Cell Content Area Start |
| `0x6B`      | `00`   | Fragmented Free Bytes   |

---

## 3. Cell Pointer Array

| Offset      | Value  |
| ----------- | ------ |
| `0x6C-0x6D` | `0FB2` |

This points directly to the schema cell stored near the end of Page 1.

---

## 4. Schema Cell Analysis

Located at:

```text
0x00000FB2
```

### Extracted Information

| Field          | Value                                                   |
| -------------- | ------------------------------------------------------- |
| Object Type    | `table`                                                 |
| Table Name     | `users`                                                 |
| Root Page      | `2`                                                     |
| SQL Definition | `CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT)` |

### Meaning of Root Page

The value:

```text
02
```

indicates that all data records belonging to the `users` table are stored inside **Page 2**.

---

# Page 2: Data B-Tree Node

Page 2 begins at:

```text
0x00001000
```

---

## 1. B-Tree Header

```text
00001000:
0d 00 00 00 06 0f c3 00 ...
```

### Header Breakdown

| Offset          | Value  | Meaning                    |
| --------------- | ------ | -------------------------- |
| `0x1000`        | `0D`   | Table Leaf Node            |
| `0x1001-0x1002` | `0000` | No Free Blocks             |
| `0x1003-0x1004` | `0006` | Six Records                |
| `0x1005-0x1006` | `0FC3` | Start of Cell Content Area |
| `0x1007`        | `00`   | Fragmented Free Bytes      |

---

## 2. Cell Pointer Array

| Cell Index | Pointer | File Address |
| ---------- | ------- | ------------ |
| 0          | `0FF6`  | `0x00001FF6` |
| 1          | `0FEE`  | `0x00001FEE` |
| 2          | `0FE4`  | `0x00001FE4` |
| 3          | `0FD9`  | `0x00001FD9` |
| 4          | `0FCE`  | `0x00001FCE` |
| 5          | `0FC3`  | `0x00001FC3` |

### Important Observation

SQLite stores cell contents from the bottom of the page upward.

```text
0x1FF6 ← Alice
0x1FEE ← Bob
0x1FE4 ← Robin
0x1FD9 ← Monica
0x1FCE ← Tushar
0x1FC3 ← Samrat
```

---

# Record Structure

Every SQLite Table Leaf Cell follows this format:

```text
+----------------------+
| Payload Size         |
+----------------------+
| RowID                |
+----------------------+
| Record Header Size   |
+----------------------+
| Serial Type Codes    |
+----------------------+
| Actual Data Values   |
+----------------------+
```

---

# Example Cell Analysis

---

## Record: Alice

Address:

```text
0x00001FF6
```

Bytes:

```text
08 01 03 00 17 41 6C 69 63 65
```

| Byte(s)          | Meaning                      |
| ---------------- | ---------------------------- |
| `08`             | Payload Size                 |
| `01`             | RowID = 1                    |
| `03`             | Header Size                  |
| `00`             | Column `id` uses RowID Alias |
| `17`             | Text Length Descriptor       |
| `41 6C 69 63 65` | "Alice"                      |

### Decoded Record

```text
id   = 1
name = Alice
```

---

## Record: Samrat

Address:

```text
0x00001FC3
```

Bytes:

```text
09 06 03 00 19 53 61 6D 72 61 74
```

| Byte(s)             | Meaning                      |
| ------------------- | ---------------------------- |
| `09`                | Payload Size                 |
| `06`                | RowID = 6                    |
| `03`                | Header Size                  |
| `00`                | Column `id` aliased to RowID |
| `19`                | Text Length Descriptor       |
| `53 61 6D 72 61 74` | "Samrat"                     |

### Decoded Record

```text
id   = 6
name = Samrat
```

---

# Reconstructed User Table

Following all six cell pointers allows complete reconstruction of the table contents.

| Pointer Address | RowID | id | name   |
| --------------- | ----- | -- | ------ |
| `0x1FF6`        | 1     | 1  | Alice  |
| `0x1FEE`        | 2     | 2  | Bob    |
| `0x1FE4`        | 3     | 3  | Robin  |
| `0x1FD9`        | 4     | 4  | Monica |
| `0x1FCE`        | 5     | 5  | Tushar |
| `0x1FC3`        | 6     | 6  | Samrat |

---

# Key Takeaways

✅ SQLite databases are organized into fixed-size pages.

✅ Page 1 contains the database header and schema metadata. 

✅ The `users` table is rooted at Page 2.

✅ Table Leaf Nodes store records as cells referenced through a Cell Pointer Array.

✅ SQLite stores record payloads from the bottom of a page upward while pointers grow downward.

✅ By following page headers, cell pointers, and record payloads, an entire SQLite database can be reconstructed directly from a raw hex dump. 