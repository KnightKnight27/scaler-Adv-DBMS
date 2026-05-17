name: Kushal S
roll no: 24bcs10355

# syscall-fileio

A C++ program that reads and writes files using **raw Linux syscalls only** — no `<stdio.h>`, no `<iostream>`, no `<fstream>`. Not even `printf`.

---

## Files

| File | Purpose |
|------|---------|
| `fileio.cpp` | The program — pure syscall I/O |
| `input.txt` | Source file the program reads |
| `output.txt` | Destination file the program writes (generated on run) |
| `README.md` | This document |

---

## Build & Run

```bash
g++ -o fileio fileio.cpp
./fileio
```

No flags needed. No dependencies. Runs on any Linux x86-64 system.

---

## The Journey of a File Operation

When you call `read()` or `write()`, the path from your C++ code to actual storage looks like this:

```
C++ code (user-space)
        │
        │  syscall instruction (SYSCALL on x86-64)
        ▼
  Linux Kernel — syscall dispatcher
        │
        ▼
  VFS Layer (Virtual File System)
        │  unified API over all filesystem types
        ▼
  Filesystem Driver (ext4 / xfs / btrfs / tmpfs …)
        │  manages inodes, blocks, journals
        ▼
  Block Layer  (I/O scheduler, request queue)
        │
        ▼
  Device Driver (NVMe / SATA / virtio-blk …)
        │
        ▼
  Hardware (SSD NAND flash / HDD platters)
```

---

## Step-by-Step Breakdown

### 1. `open("input.txt", O_RDONLY)` — Open for reading

1. **Path resolution** — kernel walks the directory tree through the **dcache** (directory entry cache). On a miss, it reads directory blocks from disk.
2. **Inode lookup** — finds the inode: stores permissions, size, timestamps, and pointers to data blocks.
3. **Permission check** — compares file's UID/GID/mode against the calling process's credentials.
4. **File description** — kernel allocates a `struct file` in kernel memory and a **file descriptor** (fd) — the smallest available non-negative integer in the process's fd table.
5. **Returns fd** — back to user-space (e.g. `3`).

### 2. `read(fd, buf, count)` — Read bytes

1. **Mode switch** — `SYSCALL` instruction traps into ring 0 (kernel mode).
2. **File position** — kernel reads the current offset (starts at 0 after `open`).
3. **Page cache check**:
   - **Hit** → data already in RAM; kernel copies pages → user buffer. Fast (nanoseconds).
   - **Miss** → kernel submits a block I/O request; process sleeps; device driver fires DMA transfer; interrupt wakes the process; data copied from newly filled page.
4. **Advance offset** — file position += bytes actually read.
5. **Returns** bytes read, `0` on EOF, `-1` on error.

### 3. `close(fd)` — Close input

1. Decrements reference count on the `struct file`.
2. At zero: frees the file description, releases the fd number.
3. **Does not flush dirty data** — page cache writeback is asynchronous.

### 4. `open("output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)` — Open for writing

- **`O_CREAT`** — if no inode exists, kernel allocates one, writes it to the filesystem journal (for crash consistency), and adds a directory entry in the parent dir.
- **`O_TRUNC`** — if the file exists, sets size to 0 and frees data blocks (copy-on-write filesystems like btrfs handle this differently).
- **`0644`** — sets permissions: owner `rw-`, group `r--`, others `r--`.

### 5. `write(fd, buf, count)` — Write bytes

1. **Mode switch** — `SYSCALL` instruction into ring 0.
2. **Copy to page cache** — kernel copies data from user buffer → kernel page (now marked **dirty**).
3. **Inode update** — file size and modification time updated in memory.
4. **Returns immediately** — data is **NOT on disk yet**. This is the **write-back** model.
5. **Eventual flush** — the kernel's `flusher` thread writes dirty pages to disk based on `vm.dirty_expire_centisecs` (default ~30 s) or when memory pressure hits.
   - Need durability guarantees? Call `fsync(fd)` before `close()`.

### 6. `close(fd)` — Close output

1. Releases the fd.
2. May flush metadata (size, timestamps) to the filesystem journal.
3. Dirty page data still flushes asynchronously unless `fsync` was called.

---

## Why No Library I/O?

| Layer | What it does |
|-------|-------------|
| `fopen` / `iostream` | Buffering in user-space, format parsing, locale handling |
| `libc` | Wraps syscalls, provides `errno`, thread safety |
| **syscall** ← *we're here* | Raw kernel interface, no overhead |

Calling syscalls directly removes every layer of abstraction between your code and the OS. Useful for:
- Understanding exactly what happens at the OS boundary
- Environments where libc is not available (early boot, firmware, minimal containers)
- Writing your own standard library

---

## Syscall Numbers (Linux x86-64)

| Syscall | Number |
|---------|--------|
| `read`    | 0 |
| `write`   | 1 |
| `open`    | 2 |
| `close`   | 3 |
| `openat`  | 257 |

The program uses `openat` (257) via `SYS_openat` with `AT_FDCWD`, which is the modern preferred form of `open`.

---

## Key Kernel Concepts Touched

- **VFS (Virtual File System)** — uniform interface over all filesystem types
- **inode** — per-file metadata node (permissions, size, block map)
- **dcache** — directory entry cache for fast path resolution
- **Page cache** — kernel's in-RAM cache of file data
- **Dirty pages** — pages modified in cache but not yet written to disk
- **Write-back** — deferred flushing of dirty pages by flusher threads
- **File descriptor table** — per-process table mapping integers → `struct file`
- **Filesystem journal** — write-ahead log for crash consistency (ext4/xfs)
