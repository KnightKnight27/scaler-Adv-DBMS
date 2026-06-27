# Lab 2 — SQLite3 Internals: mmap, page_size, PRAGMA + PostgreSQL vs SQLite3 Design Doc

## Overview

This lab explores the internal architecture of SQLite3, including its B-Tree page storage, PRAGMA configuration system, WAL journaling, and memory-mapped I/O. It also provides a comprehensive comparison between PostgreSQL and SQLite3.

## Contents

| File | Description |
|------|-------------|
| `sqlite_internals.py` | Python script that explores SQLite3 internals interactively |
| `mmap_analysis.sh` | Shell script to trace SQLite's I/O with strace |
| `design_comparison.md` | PostgreSQL vs SQLite3 architectural comparison |

## Running the Lab

### Prerequisites
- Python 3.6+ (with built-in `sqlite3` module)
- Linux system (for strace analysis)
- `sqlite3` CLI tool (for mmap_analysis.sh)
- `strace` (for I/O tracing)

### Python Script
```bash
python3 sqlite_internals.py
```

The script will:
1. Display all PRAGMA settings and their meanings
2. Parse and display the 100-byte database file header
3. Create tables and analyze B-Tree page structure
4. Demonstrate WAL mode vs rollback journal
5. Show mmap configuration and trade-offs
6. Display EXPLAIN QUERY PLAN output for various queries

### strace Analysis
```bash
chmod +x mmap_analysis.sh
bash mmap_analysis.sh
```

This traces SQLite3 CLI operations with strace and shows:
- How many syscalls each operation generates
- mmap regions created by SQLite
- fsync calls for durability
- File open/close patterns

## Key Concepts

### PRAGMA Settings
SQLite uses `PRAGMA` statements to configure runtime behavior:

| PRAGMA | What It Controls |
|--------|-----------------|
| `page_size` | B-Tree page size (4096 default, range 512-65536) |
| `journal_mode` | Transaction logging: DELETE, WAL, MEMORY, OFF |
| `mmap_size` | Max memory-mapped region (0 = disabled) |
| `synchronous` | Sync aggressiveness: OFF, NORMAL, FULL, EXTRA |
| `cache_size` | In-memory page cache size |
| `auto_vacuum` | Whether freed pages are reclaimed |

### Database File Structure
```
Page 1: 100-byte header + sqlite_master schema table
Page 2+: B-Tree nodes (interior/leaf), overflow pages, freelist pages
```

Every table and index is a separate B-Tree rooted at a specific page number (stored in `sqlite_master.rootpage`).

### WAL vs Rollback Journal

| Aspect | Rollback Journal | WAL Mode |
|--------|-----------------|----------|
| Write process | Copy original page to journal, modify in-place | Append new page to WAL file |
| Read during write | Blocked | Allowed (snapshot isolation) |
| Crash recovery | Copy journal back to restore | Ignore uncommitted WAL frames |
| Checkpoint | Not needed | Required (WAL → main DB) |

## Design Comparison

See [design_comparison.md](design_comparison.md) for a full architectural comparison covering:
- Process architecture (multi-process vs embedded)
- Storage engines (heap vs B-Tree)
- Concurrency control (MVCC vs file locking)
- WAL implementation differences
- Buffer pool strategies
- Indexing approaches
- Query processing pipelines
