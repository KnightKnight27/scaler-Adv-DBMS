# Lab 2 - SQLite3 Internals

## Files

- commands.sql
- observations.md

## Run SQLite

```bash
sqlite3 students.db
```

Execute commands:

```bash
.read commands.sql
```

---

## Verify mmap Behaviour

Without mmap:

```bash
strace -e trace=mmap,open,read sqlite3 students.db
```

With mmap enabled:

```bash
strace -e trace=mmap,open,read sqlite3 students.db
```

Observe:

- mmap() calls appear.
- Fewer read() system calls.

---

## Verify SQLite Library Architecture

Check processes:

```bash
ps aux | grep sqlite
```

Check dynamic linking:

```bash
ldd $(which sqlite3)
```

Expected:

```text
libsqlite3.so.0
```

This confirms SQLite runs as a library rather than a standalone server process.