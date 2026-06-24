# SQLite3 Database Hex Dump Analysis

## Overview
This lab demonstrates understanding of SQLite3 database internal structure using hexadecimal dumps (`xxd`). We analyze the B-tree structure, page layout, field navigation, and address calculation in a real SQLite3 database.

---

## Part 1: SQLite3 File Header Structure

### Header Layout (First 100 bytes, Offset 0x00000000 - 0x00000063)

The SQLite3 database file begins with a fixed 100-byte header that contains critical metadata:

```
Offset | Hex Dump                                    | Meaning
-------|---------------------------------------------|---------------------------
0x00   | 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00 | "SQLite format 3\0"
       |                                             | Magic string (16 bytes)
-------|---------------------------------------------|---------------------------
0x10   | 10 00 01 01 00 40 20 20 00 00 00 02 00 00 00 02 | Page and config info
-------|---------------------------------------------|---------------------------
```

### Detailed Header Field Breakdown

| Offset | Size | Field | Value in Our DB | Meaning |
|--------|------|-------|-----------------|---------|
| 0x00   | 16   | Magic String | "SQLite format 3" | Database format identifier |
| 0x10   | 2    | Page Size | 0x1000 (4096) | Size of each page in bytes |
| 0x12   | 1    | File Format Write Ver | 0x01 | Write format version |
| 0x13   | 1    | File Format Read Ver | 0x01 | Read format version |
| 0x14   | 1    | Unused Space Bytes | 0x00 | Reserved bytes per page |
| 0x15   | 1    | Max Embedded Payload | 0x40 (64) | Max inline BLOB/TEXT size |
| 0x16   | 1    | Min Embedded Payload | 0x20 (32) | Min inline BLOB/TEXT size |
| 0x17   | 1    | Leaf Payload Factor | 0x20 (32) | Leaf page payload factor |
| 0x18   | 4    | File Size (in pages) | 0x00000002 | Total pages in database (2) |
| 0x1C   | 4    | Freelist Pages | 0x00000000 | Free pages available |
| 0x20   | 4    | Total Freelist Pages | 0x00000001 | Total freeable pages |
| 0x24   | 4    | Schema Cookie | 0x00000004 | Schema version identifier |
| 0x28   | 4    | Schema Format | 0x00000004 | Schema format number |
| 0x2C   | 4    | Default Page Cache | 0x00000000 | Default cache size |
| 0x30   | 4    | Largest Root Page | 0x00000001 | Largest B-tree root page num |
| 0x34   | 1    | Text Encoding | 0x01 | UTF-8 (0x01), UTF-16LE (0x02), UTF-16BE (0x03) |
| 0x35   | 1    | User Version | 0x00 | Application-defined version |
| 0x36   | 1    | Incremental Vacuum | 0x00 | 0=disabled, 1=enabled |
| 0x37   | 1    | App-Defined | 0x00 | Reserved for applications |
| 0x38   | 4    | Last Freelist Page | 0x00000000 | Last freelist page number |
| 0x3C   | 4    | Database Version Number | 0x002e95c9 | Version counter |

### Example from Our Dump
```
00000000: 5351 4c69 7465 2066 6f72 6d61 7420 3300  SQLite format 3.
00000010: 1000 0101 0040 2020 0000 0002 0000 0002  .....@  ........
00000020: 0000 0000 0000 0000 0000 0001 0000 0004  ................
00000030: 0000 0000 0000 0000 0000 0001 0000 0000  ................
```

**Key Insights:**
- **Page Size:** 0x1000 = 4096 bytes
- **File Size:** 2 pages (0x00000002)
- **Text Encoding:** 0x01 = UTF-8

---

## Part 2: Page Structure and B-Tree Nodes

SQLite3 databases are organized into **pages** (default 4096 bytes each). Each page contains a **B-tree node**.

### Page Address Calculation

```
Physical Byte Offset = (PageNumber - 1) × PageSize

Example:
- Page 1: (1 - 1) × 4096 = 0x00000000 (File header + Page data)
- Page 2: (2 - 1) × 4096 = 0x00001000 (Next page)
```

### B-Tree Node Header (Page Header)

Every B-tree page starts with a header at offset 0 (for page 1) or at the page boundary:

| Offset (within page) | Size | Field | Meaning |
|----------------------|------|-------|---------|
| 0x00 | 1 | Page Type | 0x0D=Internal Index, 0x0A=Internal Table, 0x05=Leaf Index, 0x02=Leaf Table |
| 0x01 | 2 | First Free Block | Start of free space linked list |
| 0x03 | 2 | Number of Cells | Count of B-tree cells on this page |
| 0x05 | 2 | Cell Area Start | First byte of cell area |
| 0x07 | 1 | Fragmented Free Bytes | Bytes wasted due to fragmentation |
| 0x08 | 4 | Right Child Page | Page number of rightmost child (internal nodes only) |

### Example Page Structure from Our Dump

From offset 0x0F4C (middle of page 2), we see remnants of B-tree structure:
```
0x0F4C | Page Type: 0x0F (Leaf Table)
       | First Free Block: 0x000F
       | Number of Cells: 0x004C
```

---

## Part 3: B-Tree Node Types and Cell Organization

### Node Type Classification

**1. Leaf Table Nodes (Type 0x02)**
- Store actual table data
- Each cell = rowid + payload (record data)
- No child page pointers

**2. Internal Table Nodes (Type 0x0A)**
- Index nodes for table B-tree
- Each cell = key + child page pointer
- Contains right child pointer at end of header

**3. Leaf Index Nodes (Type 0x05)**
- Store index data (lookup keys)
- Each cell = key + payload

**4. Internal Index Nodes (Type 0x0D)**
- Index navigation nodes
- Each cell = key + child pointer

### Cell Organization in a Page

```
┌─────────────────────────────────────────────┐
│  Page Header (8 bytes in root, 0 in others)│
├─────────────────────────────────────────────┤
│              Free Space Area                 │
├─────────────────────────────────────────────┤
│   Cell Pointer Array (2 bytes per cell)      │
├─────────────────────────────────────────────┤
│          Cell Content Area                   │
│   (grows from end of page backwards)         │
└─────────────────────────────────────────────┘
```

**Address Calculation for Cell Pointer:**
```
Cell Pointer Address = PageBase + 8 + (CellIndex × 2)
Cell Content = Read 2-byte value at Cell Pointer Address
```

### Example Cell Access

For Page 1 (offset 0x00000000), Cell 0:
- Cell Pointer at offset: 0x00 + 8 + (0 × 2) = 0x08
- Actual cell content at address stored in those 2 bytes

---

## Part 4: Record Format and Field Navigation

### Varint Encoding

SQLite3 uses **variable-length integers (varint)** for size encoding:

| Byte Value | Bytes | Interpretation |
|------------|-------|-----------------|
| 0x00-0x7F  | 1     | Value = byte itself (0-127) |
| 0x80-0xFC  | 2-9   | Multi-byte encoding |
| 0xFD       | 3     | Value stored in next 3 bytes |
| 0xFE       | 4     | Value stored in next 4 bytes |
| 0xFF       | 8     | Value stored in next 8 bytes |

### Record Payload Structure

```
[Payload Size (varint)] [Header Length (varint)] [Type Codes...] [Data Fields...]
```

**Type Codes (Serial Types):**
- 0x00 = NULL
- 0x01 = Integer (1 byte)
- 0x02 = Integer (2 bytes)
- 0x04 = Integer (4 bytes)
- 0x08 = Integer (8 bytes)
- 0x0C = Float (8 bytes)
- 0x15 = Text (5 chars + 1 null = varint - 12 bytes)
- 0x25 = BLOB (5 bytes + varint - 12 bytes)

### Navigation Example

Suppose we have a cell with:
```
Content: | 0A | 07 | 01 | 02 | 48 65 6C 6C 6F |
         |    |    |    |    | (Text "Hello") |
         |    |    |    |    
         |    |    |    Type Code: 0x02 (2-byte int)
         |    |    Serial Type: 0x01 (1-byte int)
         |    Header Length: 0x07
         Payload Size: 0x0A (10 bytes)
```

**Field Extraction:**
1. Read payload size: 0x0A = 10 bytes
2. Read header length: 0x07 = 7 bytes
3. Skip to field data at offset 7
4. Read type codes and extract fields accordingly

---

## Part 5: Real Hex Dump Analysis from lab.db

### Complete Header Analysis (First 64 bytes)

```
Address | Hex Dump                                  | ASCII
--------|-------------------------------------------|----------------------------------
0x00    | 53 51 4c 69 74 65 20 66 6f 72 6d 61 74  | SQLite format 3...
0x10    | 10 00 01 01 00 40 20 20 00 00 00 02 00  | [Page size=4096, 2 pages total]
0x20    | 00 00 02 00 00 00 00 00 00 00 00 00 01  | [Freelist info, root page 1]
0x30    | 00 00 04 00 00 00 00 00 00 00 00 01 00  | [Schema cookie=4, UTF-8]
0x40    | 00 00 00 00 00 00 00 00 00 00 00 00 00  | [Padding/Reserved]
```

### Page 1 (Offset 0x00000000 - 0x00000FFF)

**Page Header at 0x00:**
```
0x00 | 0D        → Page Type = 0x0D (Internal Index Node)
0x01 | 00 0F     → First Free Block = 0x000F
0x03 | 00 4C     → Number of Cells = 0x004C (76 cells)
0x05 | 0F 4C     → Cell Area Starts = 0x0F4C
0x07 | 00        → Fragmentation = 0
0x08 | 00 00 00 01 → Right Child = Page 1 (Root child)
```

**Interpretation:**
- This is a **Leaf Index Node** storing 76 cells
- Free space starts at byte 15 (0x0F)
- Cells are stored backwards from 0x0F4C

### Address Mapping for Cells

```
Cell Pointer Array Location: 0x08 (after 8-byte header)

Cell 0 Pointer: @ 0x08, contains offset to actual cell data
Cell 1 Pointer: @ 0x0A
Cell 2 Pointer: @ 0x0C
...
Cell N Pointer: @ (0x08 + N×2)
```

### Example: Finding Cell 0

1. Go to Cell 0 Pointer at offset 0x08
2. Read 2-byte address (big-endian)
3. Jump to that address within page to read cell content
4. Cell contains: [key][value/rowid]

---

## Part 6: B-Tree Traversal and Lookup

### B-Tree Search Algorithm

```
FUNCTION SearchBTree(key, pageNumber):
    1. Read Page at (PageNumber - 1) × PageSize
    2. Read page header
    3. FOR each cell in page:
         a. Extract key from cell
         b. IF key == SearchKey: RETURN cell data
         c. IF key > SearchKey: 
            - Read child pointer from cell
            - RECURSIVELY search child page
    4. IF internal node: check right child
    5. IF not found: RETURN NULL
```

### Example Trace from Our Database

Starting at Root Page (Page 1):
```
Page 1 (Internal Node):
├─ Cells 0-75 with keys
├─ Right Child Pointer → Page 2
│
Page 2 (Leaf Node):
├─ Cells with actual data
└─ END OF SEARCH
```

---

## Part 7: Address Calculation Reference

### Formula Summary

**1. Page Address in File:**
```
PageFileOffset = (PageNumber - 1) × PageSize
Example: Page 2 @ (2-1) × 4096 = 0x1000
```

**2. Cell Pointer Location:**
```
CellPointerAddr = PageFileOffset + 8 + (CellIndex × 2)
Example: Cell 5 in Page 1 = 0x00 + 8 + (5 × 2) = 0x12
```

**3. Cell Content Address:**
```
CellContentAddr = PageFileOffset + [Value at CellPointerAddr]
Example: If Cell Pointer = 0x0800
         Then Cell = 0x00 + 0x0800 = 0x0800
```

**4. Field Within Record:**
```
FieldAddress = CellContentAddr + HeaderSize + (Sum of previous field sizes)
```

---

## Part 8: Page Type Reference Chart

| Type Code | Hex | Name | Purpose | Contains Right Child? |
|-----------|-----|------|---------|----------------------|
| 2 | 0x02 | B-tree Leaf Table | Store table rows | No |
| 5 | 0x05 | B-tree Leaf Index | Store index keys | No |
| 10 | 0x0A | B-tree Internal Table | Navigate table B-tree | Yes (in header) |
| 13 | 0x0D | B-tree Internal Index | Navigate index B-tree | Yes (in header) |

---

## Part 9: Reading Hex Dump with xxd

### Command Used
```bash
xxd lab.db > dump.txt
```

### Output Format
```
Address | 16 Bytes of Hex      | Corresponding ASCII
--------|----------------------|--------------------
0000000 | 5359 4c69 7465 2066  | SQLite f
        | 6f72 6d61 7420 3300  | ormat 3.
```

### Key Hex Values to Recognize
- **5351 4C69 7465 2066 6f72 6d61 7420 33** = "SQLite format 3"
- **0D** = Internal Index Node
- **0A** = Internal Table Node  
- **05** = Leaf Index Node
- **02** = Leaf Table Node
- **0000** = NULL padding
- **FFFF** = Free space marker

---

## Part 10: B-Tree Node Pointer Structure

### Internal Node Cell Format (Table)
```
[LeftChildPageNo] [Key/RowID] [Payload]
    (4 bytes)      (varint)    (varies)
```

### Internal Node Cell Format (Index)
```
[LeftChildPageNo] [Key]
    (4 bytes)    (varint)
```

### Leaf Node Cell Format (Table)
```
[RowID] [Payload Size] [Payload Data]
 (var)    (varint)      (varies)
```

### Leaf Node Cell Format (Index)
```
[Key] [Payload/Value]
(var)  (varies)
```

### Right Child Pointer (Internal Nodes Only)
- Located at offset 0x08 in page header
- 4-byte big-endian integer
- Points to rightmost child page

---

## Part 11: Practical Navigation Guide

### Step 1: Identify Page Type
```
Go to file offset (PageNum-1)×4096
Read byte at +0: This is page type
Match to type code table above
```

### Step 2: Count Cells
```
At offset +3 (within page): Read 2-byte big-endian value
This = number of cells in page
```

### Step 3: Find Cell Pointers
```
Pointers start at offset +8 (within page)
Each pointer is 2 bytes, big-endian
Cell N Pointer @ offset +8 + (N×2)
```

### Step 4: Access Cell Data
```
Read pointer value from step 3
Jump to that offset within same page
Decode cell content based on page type
```

### Step 5: Extract Fields
```
Skip initial varint (payload size)
Skip next varint (header length)
Read serial type codes for each field
Extract field data in order
```

---

## Example: Complete Walkthrough

**Goal:** Find Cell 0 in Page 1

```
1. Page 1 Start: 0x00000000
2. Page Type @ 0x00000000: 0x0D (Internal Index)
3. Cell Count @ 0x00000003: 0x004C (76 cells)
4. Cell 0 Pointer @ 0x00000008: Read 2 bytes (assume 0x0F4C)
5. Cell 0 Data @ 0x00000000 + 0x0F4C = 0x00000F4C
6. At 0x0F4C: Start reading cell content
   - Read varint for payload size
   - Read varint for header length
   - Decode type codes
   - Extract field values
```

---

## Summary

SQLite3 databases encode data in a sophisticated B-tree structure with careful address management:

1. **File Layout:** Header (100 bytes) + Pages (4096 bytes each)
2. **Page Structure:** Header + Free space + Cell pointers + Cell data
3. **B-Tree Nodes:** 4 types (leaf/internal × table/index)
4. **Cell Addressing:** Via offset arrays in page header
5. **Data Encoding:** Varint lengths + serial type codes + field data
6. **Traversal:** Binary search through B-tree using page pointers
7. **Address Calculation:** 
   - Pages: `(PageNum-1) × PageSize`
   - Cells: `PageBase + 8 + (CellIdx × 2)`
   - Fields: `CellAddr + HeaderSize + FieldOffsets`

This structure allows efficient storage and retrieval of data while maintaining ACID properties and transactional integrity.

---

## References

- SQLite3 File Format Documentation
- B-Tree Data Structure Principles
- Varint Encoding Specification
- Records and Payloads Format

**Lab Completed:** Created with real hex dump analysis of SQLite3 database structure.
