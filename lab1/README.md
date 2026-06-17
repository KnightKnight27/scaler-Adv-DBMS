# Low-Level File I/O Using Linux Syscalls in C++

**Author:** Jinesh Gandhi  
**Roll No:** 24BCS10072

A minimal C++ program that performs file reading and writing exclusively through raw Linux system calls — completely bypassing standard library facilities like `<cstdio>`, `<iostream>`, and `<fstream>`.

## Repository Contents

| File | Description |
|------|-------------|
| `fileio.cpp` | Core implementation — all I/O through direct syscalls |
| `input.txt` | Sample source file that gets read by the program |
| `output.txt` | Generated destination file produced after execution |
| `README.md` | Documentation and detailed technical walkthrough |

## Compilation & Execution

```bash
g++ -o fileio fileio.cpp
./fileio
```

No external dependencies or special compiler flags are necessary. Compatible with any x86-64 Linux system.

## How a File Operation Traverses the System Stack

Every `read()` or `write()` invocation triggers a chain of operations spanning from user-space down to physical storage:

```
User-space C++ application
        │
        │  SYSCALL instruction (x86-64)
        ▼
Kernel — System call dispatch table
        │
        ▼
Virtual File System (VFS) — Abstraction layer
        │  Provides a uniform interface regardless of underlying FS
        ▼
Filesystem implementation (ext4 / xfs / btrfs / tmpfs ...)
        │  Handles inode management, block allocation, journaling
        ▼
Block I/O subsystem (I/O scheduler, request merging)
        │
        ▼
Device driver layer (NVMe / SATA / virtio-blk ...)
        │
        ▼
Physical storage media (NAND flash / magnetic platters)
```

## Detailed Walkthrough of Each Operation

### Phase 1: Opening the Source File — `open("input.txt", O_RDONLY)`

The kernel performs several coordinated steps when a file is opened:

- **Directory traversal** — the kernel walks the filesystem hierarchy using the dcache (directory entry cache). On a cache miss, directory blocks are fetched from disk.
- **Inode resolution** — locates the file's inode, which encapsulates all metadata: ownership, permissions, size, timestamps, and data block pointers.
- **Access control** — the kernel cross-references the inode's UID/GID/mode bits against the calling process's credentials.
- **Resource allocation** — a `struct file` object is instantiated in kernel memory, and the smallest unused non-negative integer is assigned as the file descriptor (fd) within the process's fd table.
- **Return path** — the fd integer (e.g., 3) is passed back to the calling process.

### Phase 2: Extracting File Contents — `read(fd, buf, count)`

- **Privilege escalation** — the SYSCALL instruction transitions the CPU from ring 3 (user mode) to ring 0 (kernel mode).
- **Offset tracking** — the kernel consults the current file position (initialized to 0 after opening).
- **Page cache lookup**:
  - *Cache hit* → data is already resident in RAM; the kernel performs a direct copy from kernel pages to the user-space buffer (nanosecond-scale latency).
  - *Cache miss* → the kernel dispatches a block I/O request; the process is suspended; the device driver initiates a DMA transfer; an interrupt signals completion; data is placed in the page cache and then copied to user-space.
- **Position advancement** — the file offset is incremented by the number of bytes successfully transferred.
- **Return value** — number of bytes read, 0 when EOF is reached, or -1 on failure.

### Phase 3: Releasing the Input Handle — `close(fd)`

- The reference count on the corresponding `struct file` is decremented.
- When the count drops to zero, the kernel deallocates the file description structure and recycles the fd number.
- Crucially, this does **not** trigger a disk flush — cached page writeback is handled asynchronously by separate kernel threads.

### Phase 4: Creating/Opening the Destination — `open("output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)`

- `O_CREAT` — if no inode exists for this path, the kernel provisions a fresh one, persists it through the filesystem journal (ensuring crash safety), and inserts a new directory entry under the parent directory.
- `O_TRUNC` — if the file already exists, its logical size is reset to zero and previously allocated data blocks are freed. (Copy-on-write filesystems like btrfs implement this differently.)
- `0644` — permission mask: owner gets read+write, group and others get read-only.

### Phase 5: Persisting Data — `write(fd, buf, count)`

- **Mode transition** — another SYSCALL instruction switches back into kernel mode.
- **Page cache insertion** — the kernel transfers data from the user-space buffer into kernel pages, marking them as "dirty."
- **Metadata refresh** — the inode's size and modification timestamp are updated in memory.
- **Deferred persistence** — `write()` returns immediately. The data has **not** been committed to persistent storage yet. This is the write-back model.
- **Background flushing** — the kernel's flusher threads (historically called pdflush) eventually commit dirty pages to disk, governed by `vm.dirty_expire_centisecs` (typically around 30 seconds) or triggered by memory pressure.
- For applications requiring immediate durability, `fsync(fd)` should be invoked before closing.

### Phase 6: Finalizing Output — `close(fd)`

- Releases the file descriptor, similar to Phase 3.
- May trigger a metadata flush (timestamps, file size) to the filesystem journal depending on the filesystem implementation.
- Actual data writeback from dirty pages still proceeds asynchronously unless `fsync()` was explicitly called beforehand.

## Rationale for Bypassing Library I/O

| Abstraction Layer | Functionality Provided |
|---|---|
| `fopen` / `iostream` | User-space buffering, format conversion, locale handling |
| `libc` wrappers | Syscall invocation, errno management, thread safety |
| **Raw syscalls** ← *this project* | Direct kernel interface with zero overhead |

Invoking system calls directly strips away every intermediate layer between application code and the operating system. This approach is valuable for:
- Gaining a precise understanding of what occurs at the OS boundary
- Operating in environments where libc is unavailable (early boot stages, firmware, minimal container images)
- Building custom standard library implementations from scratch

## Linux x86-64 Syscall Reference

| System Call | Dispatch Number |
|---|---|
| `read` | 0 |
| `write` | 1 |
| `open` | 2 |
| `close` | 3 |
| `openat` | 257 |

This implementation relies on `openat` (number 257) invoked via `SYS_openat` with `AT_FDCWD` as the directory file descriptor, which is the recommended modern approach on current Linux kernels.

## Kernel Subsystems Exercised in This Lab

- **VFS (Virtual File System)** — provides a filesystem-agnostic API so applications don't need to know which filesystem is in use
- **Inode** — the per-file metadata structure containing permissions, size, block mappings, and timestamps
- **Dcache** — an in-memory cache of directory entries enabling fast pathname resolution
- **Page cache** — the kernel's primary mechanism for caching file data in RAM
- **Dirty pages** — cached pages that have been modified but not yet flushed to persistent storage
- **Write-back strategy** — deferred flushing of dirty pages handled by dedicated kernel flusher threads
- **File descriptor table** — a per-process mapping from integer descriptors to `struct file` objects in kernel memory
- **Filesystem journal** — a write-ahead log (used by ext4, xfs, etc.) that ensures metadata consistency after crashes

---
*Lab 1 — Jinesh Gandhi (24BCS10072)*