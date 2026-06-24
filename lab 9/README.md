# Lab 9 — Write-Ahead Logging (WAL) Manager

**Author:** Md Kaif (24BCS10221)  
**Course:** Advanced Database Management Systems  
**Language:** C++17

A robust, thread-safe, and crash-resilient implementation of a Write-Ahead Log (WAL) engine. This implementation serializes log records prepended with a record length and a `djb2` hash checksum to prevent reading corrupted log states during database recovery.

---

## Files

| File | Role |
|------|------|
| `wal_manager.cpp` | WALManager implementation with thread-safety and djb2 checksums |
| `main.cpp` | Test client showing append operations and database recovery |
| `makefile` | Quick `make run` compiler directive |

---

## Build & Run

To compile and run the WAL manager demo:

```bash
cd "lab 9"
make run
```

---

## Architecture

```
main.cpp (Client API calls)
   └── WALManager (wal_manager.cpp)
         ├── std::ofstream (Append-only write-path)
         ├── std::ifstream (Recovery read-path)
         └── std::mutex (Thread safety)
```

### 1. Serialized Record Layout

Unlike standard text logs, database WAL logs are written as binary streams for speed and integrity. Each record is serialized with a header containing its length and checksum:

```
+------------------+--------------------+---------------------------+
| Record Length    | djb2 Checksum      | Payload (Record Data)     |
| (uint32_t - 4B)  | (uint32_t - 4B)    | (Length Bytes)            |
+------------------+--------------------+---------------------------+
```

### 2. The Write-Ahead Log (WAL) Rule
The WAL protocol dictates that:
1. Every write operation is logged in the append-only log file on stable storage *before* modifications are applied to database memory or pages.
2. The log stream must be **flushed** (`fsync` or equivalent stream flush) to disk to guarantee durability before returning success.

### 3. Checksum Verification
To prevent partial writes or disk corruption from affecting recovery:
- A djb2 hash is calculated for each string record during `append`.
- During `recover()`, the parser checks if the computed hash of the read payload matches the stored checksum.
- Any mismatch or EOF truncations will safely halt parsing and skip/alert the corrupted record.

---

## Viva Quick Answers

**Q: What is the main purpose of Write-Ahead Logging?**  
To guarantee the **Atomicity** and **Durability** properties of ACID transaction states. In the event of a system crash, the database can re-process the WAL to reconstruct its state.

**Q: Why do we write the length and checksum of each log record?**  
Length prefixing allows storing variable-sized binary data. Checksums (like djb2) ensure we do not read garbage data during recovery if the server crashed midway through a write operation (partial write).

**Q: Why does the log stream flush on every append?**  
To ensure durability. If the log is only buffered in memory and a crash occurs, we lose records, violating the core WAL contract.

**Q: Is recovery thread-safe?**  
Yes, all external APIs in `WALManager` utilize `std::lock_guard<std::mutex>` to prevent concurrent reading/writing conflicts.
