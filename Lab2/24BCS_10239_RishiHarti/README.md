# Lab 2: SQLite3 Internals & System Design Analysis (PostgreSQL vs SQLite3)
**Student:** Rishi Harti  
**Roll Number:** 24BCS10239  

---

## Part 1: PostgreSQL vs SQLite3 — System Design Comparison

### 1. Architectural Model
* **SQLite3**: In-process library. No server process, no network socket, zero-configuration. The engine is compiled directly into the application process. Storage is a single binary `.db` file on disk.
* **PostgreSQL**: Robust client-server architecture. Relies on a persistent server daemon (`postgres`) waiting for connections on TCP port `5432` or UNIX domain sockets. Implements a multi-process model (forks a backend process per client connection).

### 2. Concurrency Control & Transactions
* **SQLite3**: Implements database-level locking. In journal-mode, a write operation acquires an exclusive lock, blocking all reader and writer processes. In WAL (Write-Ahead Logging) mode, concurrency improves dramatically by allowing multiple readers to co-exist with a single writer.
* **PostgreSQL**: Implements Multi-Version Concurrency Control (MVCC). Readers never block writers, and writers never block readers. Supports highly concurrent row-level locking, rich transaction isolation levels (Read Committed, Repeatable Read, and Serializable), and parallel query execution.

### 3. Data Type System
* **SQLite3**: Uses a dynamic type affinity system. Types are associated with individual values, not columns. Supports only 5 core storage classes: `NULL`, `INTEGER`, `REAL`, `TEXT`, and `BLOB`. Columns can store any type regardless of defined affinity.
* **PostgreSQL**: Strict, extremely rich static typing system. Supports numeric types, UUIDs, arrays, geometric shapes, JSONB/XML, and user-defined custom types. Enforces strict schema validations at compile/execution boundary.

### 4. Comparison Summary Matrix

| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Process Model** | Embedded Library (in-process) | Multi-process Client-Server daemon |
| **Database Storage** | Single file on local filesystem | Data directory holding multiple files and WALs |
| **Concurrency** | DB-level locks (WAL allows 1 writer + readers) | MVCC with fine-grained Row-level locks |
| **Authentication** | None (filesystem permission dependent) | Advanced Role-Based Access Control (RBAC) + SSL |
| **Best Used For** | Mobile apps, CLI tools, local configs, caches | Scalable web backends, high-write data warehouses |

---

## Part 2: SQLite3 Database Internals — Hexadecimal & B-Tree Analysis

### 1. Database Metadata
Introspection of the custom `students` database shows the following properties:
* **Page Size**: `4096 bytes` (matches OS default page size for aligned I/O)
* **Page Count**: `4` (Total database file size = `4096 * 4 = 16384 bytes`)

### 2. Physical Layout & Root Pages
SQLite maps database objects to individual B-tree root pages as follows:

| Page Number | File Offset | Database Object / Purpose |
| :---: | :--- | :--- |
| **Page 1** | `0x0000 - 0x0FFF` | **Master Page**: File Header + `sqlite_master` metadata table |
| **Page 2** | `0x1000 - 0x1FFF` | **Table B-Tree Root**: Holds physical record data for `students` table |
| **Page 3** | `0x2000 - 0x2FFF` | **Index B-Tree**: Auto-index for `students` primary key |
| **Page 4** | `0x3000 - 0x3FFF` | **Index B-Tree**: Auto-index for `students` unique constraint |

```
+-------------------------------------------------------------+
|                         students.db                         |
+-------------------------------------------------------------+
| Page 1 (0x0000): SQLite Header + sqlite_master B-Tree        |
+-------------------------------------------------------------+
| Page 2 (0x1000): students Table Leaf B-Tree (Real Rows)    |
+-------------------------------------------------------------+
| Page 3 (0x2000): Primary Key Auto-Index B-Tree              |
+-------------------------------------------------------------+
| Page 4 (0x3000): Unique Constraint Auto-Index B-Tree        |
+-------------------------------------------------------------+
```

---

## 3. SQLite Page Header Analysis (Page 2)

Dumping the beginning of Page 2 (students table leaf B-tree) shows the following header bytes:
```text
0d 00 00 00 02 0f 67 00
```

### Decoding Header Fields:
1. **`0d` (1 byte)**: Page Type Flag. `0x0D` (decimal 13) represents a **Table Leaf B-Tree Page**. This indicates the page holds actual row data payloads (rather than internal index nodes).
2. **`00 00` (2 bytes)**: Offset of the first freeblock. `0` indicates no freeblocks (no deleted space is currently fragmenting the page).
3. **`00 02` (2 bytes)**: Cell Count. There are exactly `2` records (cells) stored on this page.
4. **`0f 67` (2 bytes)**: Start of cell content area. Offset `0x0F67` (decimal 3943) marks the boundary of the unallocated free space.
5. **`00` (1 byte)**: Fragmented free bytes. There are `0` fragmented bytes on the page.

---

## 4. Page Organization & Cell Pointer Array

Directly following the page header is the **Cell Pointer Array**:
```text
0f b4
0f 67
```

### Explanation of Offsets:
- **`0f b4`**: Pointer to the first record (cell 0), located at byte offset `3943 + 77 = 4020` in the page.
- **`0f 67`**: Pointer to the second record (cell 1), located at byte offset `3943` in the page.

> [!NOTE]
> SQLite pages grow in opposite directions: **B-tree Page Headers** and **Cell Pointer Arrays** grow downwards from the top of the page, while the **Record Payloads** are appended from the bottom of the page growing upwards. The space in between is dynamic free space.

```
+---------------------------------------+
| B-Tree Page Header (8 bytes)           |
+---------------------------------------+
| Cell Pointer Array (grows downwards)  |
| [ 0x0FB4, 0x0F67 ]                    |
+---------------------------------------+
|                 <--- Free Space --->  |
+---------------------------------------+
| Record 1 Payload (grows upwards)     |
| (Offset: 0x0F67)                      |
+---------------------------------------+
| Record 0 Payload (grows upwards)     |
| (Offset: 0x0FB4)                      |
+---------------------------------------+
```

---

## 5. Record Payload Extraction & Analysis

Analyzing raw hex values at offset `0x0F67` (Record 1 payload) exposes physical student data:

* **First Name**: `4b 61 72 74 69 6b` -> Decodes to **Kartik**
* **Last Name**: `42 68 61 74 69 61` -> Decodes to **Bhatia**
* **Email Field**: `6b 61 72 74 69 6b 40 65 78 61 6d 70 6c 65 2e 63 6f 6d` -> Decodes to **kartik@example.com**
* **Course Field**: `43 6f 6d 70 75 74 65 72 20 53 63 69 65 6e 63 65` -> Decodes to **Computer Science**

### Record Storage Format:
```
+--------------------------------------------------------------------------------------+
| Payload Size | Row ID | Record Header Size | Column Serial Types | Actual Column Data |
+--------------------------------------------------------------------------------------+
```
SQLite stores records utilizing variable-length encoding (varints) for payload size and serial types, ensuring no storage bytes are wasted on empty or default fields.
