# Lab 1: Raw File I/O with System Calls

**Role Number:** `24BCS10222`
**Name:** `Mohit Kumar`

## Program Overview

- Source file: `file_syscalls.cpp`
- Input file: `input.txt`
- Output file: `output.txt`
- Behavior:
  - Seeds `input.txt` with sample content on first run (using `O_EXCL` to avoid overwriting)
  - Opens `input.txt` for reading and `output.txt` for writing
  - Copies data in 8 KB chunks using `read()` + `write()` loop
  - Closes all file descriptors on success or error

## Build and Run

```bash
g++ -std=c++17 file_syscalls.cpp -o file_syscalls
./file_syscalls
```

Expected output:

```
Copy complete.
```

---

## Learning Notes

### 1) File Descriptors

- A file descriptor (FD) is a non-negative integer the kernel assigns when a process opens a file.
- The kernel maintains a per-process FD table; each entry points to an open-file object tracking the current offset, flags, and the underlying inode.
- Standard FDs: `0` = stdin, `1` = stdout, `2` = stderr.
- `open()` returns the lowest available FD. `read()`, `write()`, `close()` all take an FD as the first argument.
- Unclosed FDs are a resource leak — the kernel holds the file open until `close()` is called or the process exits.

### 2) `strace`

- `strace` logs every system call a process makes, along with arguments and return values.
- Essential for confirming what the program actually asks the OS to do vs. what you intended.

```bash
strace -e trace=open,openat,read,write,close ./file_syscalls
```

- You'll see `openat()` for each file, `read()` fetching chunks, `write()` storing them, then `close()` for cleanup.
- Return value of `read()` tells you how many bytes were actually read — it can be less than requested.

### 3) Inodes

- Every file on a Linux filesystem has an inode: a metadata block stored on disk (or in memory for tmpfs).
- An inode holds: permissions, owner, group, size, link count, timestamps, and pointers to data blocks.
- The filename itself lives in the parent directory entry, not in the inode.
- When you call `open("input.txt", ...)`, the VFS layer resolves the name through the directory to its inode number, then builds an open-file structure around it.

### 4) Journey of a Read

1. `open("input.txt", O_RDONLY)` → CPU enters kernel via syscall; VFS resolves path → inode → permission check → returns FD.
2. `read(fd, buf, 8192)` → kernel checks the page cache for the file's pages.
3. **Cache hit:** kernel copies data directly from page cache to the user-space buffer.
4. **Cache miss:** kernel submits a block I/O request; the storage driver uses DMA to load the page from disk into the page cache; then copies to the user buffer.
5. Returns number of bytes read; `0` means EOF.
6. `close(fd)` → kernel drops the open-file reference; inode ref count decremented.

---

## Additional Concepts

### 5) Pages

- Memory is divided into fixed-size pages (4 KB on x86-64).
- The kernel's **page cache** holds recently accessed file data in RAM pages.
- Repeated reads of the same file are fast because the data is already in cache.
- `posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL)` hints to the kernel to read-ahead aggressively.

### 6) Device Drivers

- A driver is a kernel module that knows how to talk to a specific piece of hardware.
- I/O path: user program → VFS → filesystem layer → block layer → driver (NVMe/SATA/USB) → physical device.
- The program only interacts with the VFS interface; the driver layer is transparent.

### 7) DMA (Direct Memory Access)

- Without DMA, the CPU would copy each byte from the device register into RAM — extremely slow.
- With DMA, the storage controller writes data directly into a designated RAM region while the CPU does other work.
- The CPU is notified via an interrupt when the transfer is complete, then the kernel makes the data available in the page cache.

### 8) Page Cache Eviction

- The page cache grows until memory is needed elsewhere.
- Linux uses an Active/Inactive list (a two-hand clock-like algorithm) to decide which pages to evict.
- **Clean pages** (not modified) are dropped immediately.
- **Dirty pages** (modified, not yet written to disk) go through writeback before eviction.
- Eviction is why a query that ran fast the first time can be slower after a memory-hungry workload runs in between.

---

## Summary

- POSIX file I/O (`open`, `read`, `write`, `close`) gives direct, predictable control over kernel interactions.
- `strace` bridges the gap between source code and what the kernel actually executes.
- The page cache is the single biggest factor in I/O performance — a cache hit is orders of magnitude faster than a disk read.
- DMA offloads the data-movement work from the CPU when the page cache can't serve a request.
