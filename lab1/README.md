# Lab 1 — C++ File I/O: inode → VFS → Page Cache → Syscall Journey

## Overview

This lab demonstrates how high-level C++ file operations translate into Linux system calls, and how those syscalls interact with the kernel's Virtual File System (VFS), inode structures, and page cache.

By running the program under `strace`, you can observe the exact kernel-level operations that occur during file I/O.

## The I/O Journey

```
┌─────────────────────────────────────────────────────────────────┐
│                     User Space (C++ Program)                     │
│  ofstream::write()   ←→   fwrite()   ←→   write() wrapper      │
└───────────────────────────────┬─────────────────────────────────┘
                                │ syscall (software interrupt)
┌───────────────────────────────▼─────────────────────────────────┐
│                     Kernel Space                                 │
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │  sys_write()  │───▶│     VFS      │───▶│  Filesystem  │       │
│  │  sys_read()   │    │  (dispatch)  │    │  (ext4/xfs)  │       │
│  │  sys_mmap()   │    └──────────────┘    └──────┬───────┘       │
│  └──────────────┘                                │               │
│                                          ┌───────▼───────┐       │
│                                          │  Page Cache    │       │
│                                          │  (in-memory    │       │
│                                          │   page frames) │       │
│                                          └───────┬───────┘       │
│                                                  │               │
│                                          ┌───────▼───────┐       │
│                                          │  Block Layer   │       │
│                                          │  + I/O Sched   │       │
│                                          └───────┬───────┘       │
└──────────────────────────────────────────────────┼───────────────┘
                                                   │
                                           ┌───────▼───────┐
                                           │    Disk/SSD    │
                                           │  (persistent)  │
                                           └───────────────┘
```

## Key Concepts

### 1. Inodes
- Every file has an **inode** containing metadata: size, permissions, timestamps, block pointers.
- `fstat()` reads inode metadata without touching file data.
- `write()` updates the inode's `mtime` and `size` fields.

### 2. Virtual File System (VFS)
- The VFS is an abstraction layer that provides a **uniform interface** for all filesystems.
- When you call `write(fd, buf, n)`, the VFS dispatches to the correct filesystem's write handler (e.g., `ext4_file_write_iter`).

### 3. Page Cache
- The kernel maintains a **page cache** — an in-memory cache of file data organized in 4KB pages.
- **Writes**: Data goes to the page cache first (marked as "dirty"), NOT directly to disk.
- **Reads**: The kernel checks the page cache first. Cache hit = no disk I/O.
- **fsync()**: Forces dirty pages to be flushed to disk (durability guarantee).

### 4. Memory-Mapped I/O (mmap)
- `mmap()` maps file pages directly into the process's virtual address space.
- Reads/writes through the mapping go directly to/from the page cache.
- First access triggers a **page fault** → kernel loads the page from disk (or cache).

### 5. O_DIRECT
- Bypasses the page cache entirely — DMA between userspace buffer and disk.
- Used by databases (e.g., MySQL InnoDB) that manage their own buffer pool.

## Building and Running

```bash
# Build
make

# Run without tracing
make run

# Run with strace (recommended!)
make trace
# or
bash run_strace.sh

# View the syscall trace
less strace_output.log
```

## What to Look For in the strace Output

| Syscall | What It Does | Database Relevance |
|---------|-------------|-------------------|
| `openat()` | Opens file, returns fd. Kernel does path lookup → inode lookup | Connection to data files |
| `write()` | Copies data to page cache (dirty pages) | INSERT/UPDATE operations |
| `read()` | Reads from page cache (or triggers disk I/O on miss) | SELECT queries |
| `lseek()` | Repositions fd offset (no I/O) | Random access in B-Tree pages |
| `fsync()` | Flushes dirty pages + metadata to disk | WAL flush, COMMIT durability |
| `mmap()` | Maps file into virtual memory | SQLite uses this, PostgreSQL does not |
| `fstat()` | Reads inode metadata | File size checks |
| `close()` | Releases fd and inode reference | Connection cleanup |

## Sample strace Output (Annotated)

```
# Opening the file — kernel does path lookup → inode allocation
14:23:01.234 openat(AT_FDCWD, "testfile.dat", O_RDWR|O_CREAT|O_TRUNC, 0644) = 3

# Sequential writes — data goes to page cache as dirty pages
14:23:01.235 write(3, "PAGE_0_STARTAAA..."..., 4096) = 4096
14:23:01.235 write(3, "PAGE_1_STARTBBB..."..., 4096) = 4096

# fsync — flush dirty pages to disk (the durability guarantee)
14:23:01.236 fsync(3) = 0    <0.003412>  ← notice the time: disk I/O is slow!

# Random read — served from page cache (nearly instant)
14:23:01.240 lseek(3, 12288, SEEK_SET) = 12288
14:23:01.240 read(3, "PAGE_3_STARTDDD..."..., 4096) = 4096  <0.000012>  ← fast: cache hit!

# mmap — map file into process memory
14:23:01.241 mmap(NULL, 16384, PROT_READ|PROT_WRITE, MAP_SHARED, 3, 0) = 0x7f4a3c000000
```

## Exercises

1. **Count the syscalls**: How many `write()` calls does the POSIX section make vs. the iostream section? Why?
2. **Timing**: Compare the time taken by `fsync()` vs `write()`. What does this tell you about the page cache?
3. **Page cache hit**: Notice how `read()` after `write()` is nearly instant. Why?
4. **O_DIRECT**: If O_DIRECT works on your filesystem, compare its `write()` time to normal `write()`.

## Files

| File | Description |
|------|-------------|
| `file_io_demo.cpp` | Main C++ program demonstrating all I/O operations |
| `run_strace.sh` | Shell script to compile, run under strace, and analyze output |
| `Makefile` | Build targets: `all`, `run`, `trace`, `clean` |
