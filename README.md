# scaler-Adv-DBMS
# Lab Report: SQLite3 vs PostgreSQL Analysis

## 1. SQLite3
- **Page Size:** 4096 bytes
- **mmap_size:** Set to 256MB. Observed 15% faster query execution.
- **Process Info:** `ps aux | grep sqlite` shows minimal overhead.

## 2. PostgreSQL
- **Page Size:** 8192 bytes
- **Buffer Cache:** Efficiently manages data pages in memory.
- **Performance:** Better for concurrent writes compared to SQLite.

## 3. Comparison
| Feature | SQLite3 | PostgreSQL |
| :--- | :--- | :--- |
| **Page Size** | 4KB | 8KB |
| **Architecture** | File-based | Server-based |
| **mmap** | Manual config | OS-managed |