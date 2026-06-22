# SQLite B-tree Hex Dump Analysis

Inspect the raw on-disk layout of a SQLite database using `xxd` and decode the B-tree structure from the binary.

## What It Does

1. **Creates** a `books` database with 20 rows (enough to force a multi-page B-tree)
2. **Dumps** the raw bytes with `xxd`
3. **Decodes** the file header, interior node, leaf pages, cell pointer arrays, and varint payloads

## Quick Start

```bash
sqlite3 books.db < seed.sql
xxd books.db > hexdump.txt

# Verify the layout
sqlite3 books.db "PRAGMA page_size;"
sqlite3 books.db "PRAGMA page_count;"
sqlite3 books.db "SELECT pageno, pagetype, ncell, unused FROM dbstat ORDER BY pageno;"
```

## Files

| File | Description |
|------|-------------|
| `seed.sql` | Reproducible schema and data |
| `hexdump.txt` | `xxd books.db` output |
| `Assignment.md` | Full analysis: header decode, page map, interior/leaf node walkthrough, varint examples |

## Documentation

See [Assignment.md](Assignment.md) for the full byte-level walkthrough.

Praveen Kumar
24bcs10048

## Requirements

- sqlite3 (any recent version)
- xxd (comes with vim-common on most Linux distros)
