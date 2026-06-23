# Phase 1: Storage Engine Implementation

## Overview
Phase 1 implements the persistent storage layer for MiniDB. It uses a **slotted-page design** to support variable-length rows efficiently and includes a buffer pool for performance.

## Components Implemented

### 1. Slotted-Page Design (`Page.java`)
- **Reasoning**: Slotted pages allow us to store variable-length rows without moving existing rows when one is deleted or updated. The slot directory acts as an indirection layer.
- **Layout**:
    - **Header**: Contains `pageId`, `slotCount`, and a `freeSpacePointer`.
    - **Slot Directory**: Array of `(offset, length)` pairs growing upwards from the header.
    - **Row Data**: Binary row data growing downwards from the end of the page.
- **Free Space**: The gap between the slot directory and the row data.

### 2. Variable-Length Rows (`Row.java`)
- **Serialization**:
    - `INT`: 4 bytes.
    - `VARCHAR`: 4-byte length prefix + UTF-8 encoded bytes.
- **Logic**: This format allows the storage engine to handle strings of any length (up to the remaining page capacity).

### 3. Disk Management (`PageManager.java`)
- Uses `RandomAccessFile` to manage a single database file.
- Pages are fixed-size (4096 bytes), allowing $O(1)$ seek time to any `pageId` using the formula `offset = pageId * 4096`.

### 4. Caching (`BufferPool.java`)
- Implements an **LRU (Least Recently Used)** eviction policy using a `LinkedHashMap`.
- Dirty page tracking ensures that only modified pages are written back to disk upon eviction or shutdown.

### 5. Table Abstraction (`HeapFile.java`)
- Manages a collection of pages.
- **Insertion**: Scans existing pages for free space before allocating a new page.
- **Scanning**: Provides methods to iterate over all valid rows in the table.

## Verification Results
An integration test (`StorageIntegrationTest.java`) was executed with the following steps:
1.  Inserted 1000 rows with varying string lengths into a new database file.
2.  Performed a clean shutdown (flushing all pages and closing the file).
3.  Restarted the system by creating new Manager/Pool instances.
4.  Scanned the file and verified that all 1000 rows matched the original data.

**Result**: `PASS` (All 1000 rows persisted and retrieved correctly).
