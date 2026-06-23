# Lab 1 — C++ File I/O: The Kernel Journey via strace

**Student:** Romit Raj Sahu | **Roll:** 24BCS10436

---

## 1. Program Overview

`file_io.cpp` demonstrates raw POSIX file I/O using only kernel syscalls — no C++ streams or `FILE*` abstractions. It:

1. Opens `lab1_output.txt` for writing (`open` with `O_WRONLY | O_CREAT | O_TRUNC`)
2. Writes the string `"Hello from raw C++ syscalls!\n"` (29 bytes) using `write`
3. Forces the data to disk with `fsync` (or `_commit` on Windows)
4. Closes the write descriptor
5. Reopens the file for reading (`O_RDONLY`)
6. Reads the content back into a buffer with `read`
7. Closes the read descriptor

Every syscall is followed by a `printf` diagnostic so the execution trace is visible without `strace`.

---

## 2. The Full Journey of a `write()` Call

```
C++ write() call
      |
      v
glibc write() wrapper          (thin C library shim; sets up syscall number in register)
      |
      v
trap into kernel               (syscall boundary: CPU privilege ring 3 -> ring 0)
      |
      v
sys_write() in kernel          (syscall dispatch table)
      |
      v
VFS layer: vfs_write()         (Virtual File System -- filesystem-agnostic interface)
      |
      v
filesystem driver              (e.g., ext4_file_write_iter for ext4)
      |
      v
Page Cache                     (data lands in kernel RAM; pages marked "dirty")
      |
      v  (only when fsync() is called -- otherwise writeback is lazy)
Block device layer             (submit_bio -- queues request to block device driver)
      |
      v
Physical disk / NVMe           (data durably stored)
```

Without `fsync`, the data sits in the Page Cache and is written back lazily by `pdflush`/`kworker` threads on a schedule (or on memory pressure). A crash before writeback loses the data.

---

## 3. What is a File Descriptor?

A file descriptor (fd) is a small non-negative integer that a process uses to refer to an open file. Key facts:

- **0, 1, 2 are reserved**: stdin, stdout, stderr
- **Kernel assigns lowest available integer**: after `close(3)`, the next `open()` gets fd=3 again
- The fd is an **index into the process's open file table** (per-process kernel data structure)
- Each open file table entry points to an **open file description** (shared across `dup`/`fork`)
- The open file description points to the **inode** (the actual file on disk)

This layering means two processes can have different fd numbers pointing to the same file, and the same fd number in different processes refers to entirely different files.

---

## 4. What is an inode?

An inode (index node) is a kernel data structure stored in the filesystem (on disk, cached in RAM) that represents a file's metadata:

| Field | Stores |
|-------|--------|
| `st_size` | File size in bytes |
| `st_mode` | Permissions (rwxrwxrwx) and file type |
| `st_atime` | Last access time |
| `st_mtime` | Last modification time |
| `st_ctime` | Last metadata change time |
| `st_blocks` | Disk block addresses (direct + indirect) |

**The inode does NOT store the filename.** The filename lives in the directory entry (dentry), which maps name → inode number. This is why **hard links** work: two directory entries with different names can point to the same inode number. Both names refer to the same file data; the inode's link count tracks how many dentries reference it.

---

## 5. What is VFS (Virtual File System)?

VFS is a kernel abstraction layer that sits above all concrete filesystems (ext4, btrfs, tmpfs, NFS, procfs, etc.) and provides a **uniform syscall interface**.

When userspace calls `write(fd, buf, n)`, the kernel:
1. Looks up the open file description (using the fd)
2. Finds the file's `inode` and its associated `file_operations` struct
3. Calls `file_operations->write_iter` — a **function pointer** set by the specific filesystem driver

Without VFS, every filesystem would need its own set of syscalls. VFS makes the same `write()` work identically whether the file is on an ext4 partition, an NFS mount, or `/proc/self/mem`.

---

## 6. What is the Page Cache?

The Page Cache is a region of kernel RAM used to cache disk pages. It sits between the filesystem driver and physical storage.

- **Writes go here first** (write-behind / lazy write): fast because it is RAM, not disk
- **Reads also go through here** (read-ahead caching): if a page is already cached, no disk I/O
- The kernel tracks which pages are "dirty" (written but not yet on disk)
- A background writeback mechanism (`kworker` threads) eventually flushes dirty pages to disk
- On memory pressure, the kernel evicts clean (already flushed) pages using an LRU policy

The Page Cache is why database systems that need guaranteed durability must call `fsync()` explicitly.

---

## 7. What does `fsync()` do?

`fsync(fd)` tells the kernel: *"flush all dirty pages for this file to durable storage right now, and do not return until it is done."*

Effect:
- Kernel identifies all dirty pages in the Page Cache for `fd`'s inode
- Submits write requests to the block device layer
- Waits for the disk to acknowledge the write (disk's own write cache may also be flushed)
- Returns 0 on success

**Without `fsync`:** data may sit in the Page Cache for seconds or minutes. A power failure or kernel panic in that window loses the write, even though `write()` returned successfully.

**Performance cost:** `fsync` involves physical I/O, which is 100x–1000x slower than writing to RAM. Databases batch writes and use a Write-Ahead Log (WAL) so they can call `fsync` once per transaction batch rather than once per row.

On Windows, the equivalent is `_commit(fd)` (POSIX fd) or `FlushFileBuffers(HANDLE)`.

---

## 8. Syscall Summary Table

| Syscall | fd | Key arguments | Return | What it does |
|---------|----|--------------|--------|--------------|
| `openat` | N/A | `"lab1_output.txt"`, `O_WRONLY\|O_CREAT\|O_TRUNC`, `0644` | 3 (new fd) | Opens file, creates entry in fd table |
| `write` | 3 | `buf="Hello..."`, `n=29` | 29 | Copies 29 bytes into page cache; marks dirty |
| `fsync` | 3 | — | 0 | Flushes dirty pages to disk; blocks until done |
| `close` | 3 | — | 0 | Releases fd; decrements open-file-description refcount |
| `openat` | N/A | `"lab1_output.txt"`, `O_RDONLY` | 3 | Reopens file read-only; fd=3 reused |
| `fstat` | 3 | `struct stat*` | 0 | Reads inode metadata (size=29, mode=0644) |
| `read` | 3 | `buf`, `n=64` | 29 | Copies 29 bytes from page cache into userspace buffer |
| `close` | 3 | — | 0 | Releases fd again |

---

## 9. Why the `read` was a Page Cache Hit

When `read(3, buf, 64)` executes:

1. The kernel checks the Page Cache for the inode's pages
2. Those pages were **just written** by the earlier `write()` call and are still in RAM
3. No disk I/O is needed — the kernel copies directly from the cached page to the userspace buffer
4. The `fstat` output (`st_size=29`) confirms the file has exactly 29 bytes, matching the write

This is a **temporal locality** win: a file written and immediately read will almost always hit the cache. The `strace_output.txt` shows `read` returning 29 bytes instantly, without any disk access.

---

## 10. Note on strace

`strace` is a Linux-specific tool that intercepts and records syscalls made by a process. The file `strace_output.txt` in this directory shows the annotated output produced when running `file_io` on a Fedora Linux VM (kernel 6.x, ext4 filesystem).

On Windows (this development environment), the equivalent is **Process Monitor** (Sysinternals): filter by process name and operation types `CreateFile`, `WriteFile`, `ReadFile`, `CloseFile` to observe the same file activity at the Win32 API layer.

The program itself is portable: the `#ifdef _WIN32` guard switches between `_commit()` (Windows) and `fsync()` (POSIX) for the flush operation.
