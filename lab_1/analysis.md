# Lab 1: File I/O System Call Analysis

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This document analyzes the kernel-level journey of file I/O operations in C++ using the simple file reader program. We examine the syscalls involved, the inode lifecycle, VFS interaction, and the page cache mechanism.

---

## 1. Program Description

Our `reader.cpp` program performs basic file I/O operations:

```cpp
std::ifstream file("test.txt");  // Open file
std::getline(file, line);        // Read lines
// file closes automatically (destructor)
```

This high-level C++ code triggers several low-level system calls at the kernel boundary.

---

## 2. System Calls Involved

When the program executes, the following syscalls occur (in order):

### 2.1 `openat()` or `open()`
**Purpose:** Opens the file and returns a file descriptor

**What happens:**
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
```

**Kernel actions:**
1. **Path resolution:** Kernel walks the directory tree from current working directory
2. **Inode lookup:** Translates filename → inode number (70910287 in our case)
3. **Permission check:** Verifies process has read permission (0644)
4. **FD allocation:** Allocates file descriptor 3 in process's fd table
5. **Returns:** File descriptor integer to user space

**Parameters:**
- `AT_FDCWD`: Use current working directory as base
- `"test.txt"`: File path (relative)
- `O_RDONLY`: Open for reading only
- **Return value:** 3 (file descriptor)

---

### 2.2 `fstat()` or `fstat64()`
**Purpose:** Retrieve file metadata (inode information)

**What happens:**
```
fstat(3, {st_mode=S_IFREG|0644, st_size=154, ...}) = 0
```

**Information retrieved:**
- **st_ino:** Inode number (70910287)
- **st_mode:** File type and permissions (regular file, rw-r--r--)
- **st_size:** File size in bytes (154)
- **st_blocks:** Number of 512-byte blocks allocated (8)
- **st_uid/st_gid:** Owner user and group IDs
- **st_atime/st_mtime/st_ctime:** Access, modification, change timestamps

**Why it's called:** C++ iostream needs to know file size for buffer allocation and to determine EOF position.

**Cost:** Very cheap - metadata is cached in kernel's inode cache (icache).

---

### 2.3 `read()`
**Purpose:** Read bytes from file into user-space buffer

**What happens:**
```
read(3, "hello from lab 1\nthis is...", 4096) = 154
read(3, "", 4096) = 0    // EOF
```

**Parameters:**
- `3`: File descriptor
- `buffer`: Pointer to user-space buffer (managed by iostream)
- `4096`: Maximum bytes to read (typical buffer size)
- **Return value:** Actual bytes read (154 first call, 0 at EOF)

**Kernel journey:**
1. **File descriptor → inode:** Kernel dereferences fd 3 to get inode pointer
2. **VFS layer:** Virtual Filesystem Switch routes to appropriate filesystem driver
3. **Filesystem driver:** APFS/HFS+ (macOS) or ext4/btrfs (Linux) handles block lookup
4. **Page cache check:** Kernel checks if file pages are already in memory
5. **Disk I/O (if needed):** On cache miss, driver reads from physical disk
6. **Copy to user space:** Data copied from kernel buffer to user-space buffer

**Page cache optimization:** Subsequent reads of same file are served from RAM, not disk!

---

### 2.4 `mmap()` (for shared libraries)
**Purpose:** Memory-map files (C++ runtime, shared libraries) into process address space

**What happens:**
```
mmap(NULL, size, PROT_READ|PROT_EXEC, MAP_PRIVATE, fd, 0) = 0x...
```

**Usage:** Not for test.txt, but for:
- C++ standard library (libc++, libstdc++)
- System libraries (libsystem_kernel, libc)
- Dynamic linker

**Benefit:** Library code pages are shared across all processes using them.

---

### 2.5 `close()`
**Purpose:** Release file descriptor and decrement inode reference count

**What happens:**
```
close(3) = 0
```

**Kernel actions:**
1. **FD table cleanup:** Removes fd 3 from process's open file table
2. **Struct file cleanup:** Decrements reference count on kernel's `struct file`
3. **Inode reference:** Decrements inode reference count
4. **Flush if needed:** If file was modified and no other processes have it open, flush dirty pages

**Note:** In our program, `close()` is called automatically by `std::ifstream` destructor.

---

## 3. The Inode Journey

### 3.1 What is an Inode?

An **inode** (index node) is the kernel's internal representation of a file. It contains:
- File metadata (size, permissions, timestamps)
- Pointers to data blocks on disk
- Reference count (number of hard links + open file handles)

**Key insight:** The filename is NOT stored in the inode. Filenames live in directory entries, which map name → inode number.

### 3.2 Inode Details for test.txt

```bash
$ ls -i test.txt
70910287 test.txt

$ stat test.txt
Inode: 70910287
Size: 154 bytes
Blocks: 8
Permissions: 100644 (rw-r--r--)
```

### 3.3 Path Resolution Process

When `openat()` is called with "test.txt":

```
1. Start at current directory (AT_FDCWD)
   └─> Get inode of current directory

2. Look up "test.txt" in directory entries
   └─> Find entry: "test.txt" → inode 70910287

3. Fetch inode 70910287 from filesystem
   └─> Load inode structure (likely cached in icache)

4. Check permissions
   └─> Compare process UID/GID with inode's ownership
   └─> Verify O_RDONLY is allowed (st_mode & 0444)

5. Allocate file descriptor
   └─> Create struct file pointing to inode
   └─> Add to process's fd table at slot 3

6. Return fd 3 to user space
```

---

## 4. VFS Layer (Virtual Filesystem Switch)

The **VFS** is an abstraction layer that provides a uniform interface to different filesystem implementations.

### 4.1 Why VFS Exists

Without VFS, every program would need filesystem-specific code:
```cpp
// Without VFS (nightmare!)
if (fs_type == EXT4)
    ext4_open("test.txt");
else if (fs_type == APFS)
    apfs_open("test.txt");
else if (fs_type == NFS)
    nfs_open("test.txt");
```

With VFS, the kernel provides generic operations:
```
open() → vfs_open() → [filesystem-specific handler]
```

### 4.2 VFS Architecture

```
User Space (C++ program)
      |
      | open(), read(), close()
      |
─────┼─────────────────────────────── (syscall boundary)
      |
   VFS Layer (generic interface)
      |
      ├─> ext4 driver   (Linux)
      ├─> APFS driver   (macOS)
      ├─> btrfs driver  (Linux)
      ├─> NFS client    (network filesystem)
      └─> FUSE driver   (user-space filesystems)
      |
   Block Device Layer
      |
   Physical Storage (SSD/HDD)
```

### 4.3 VFS Operations

Each filesystem registers these operations:
- `open()`: Open file
- `read()`: Read data
- `write()`: Write data
- `lookup()`: Resolve filename → inode
- `stat()`: Get inode metadata
- `readdir()`: List directory entries

---

## 5. Page Cache Mechanism

The **page cache** (also called buffer cache) is the kernel's in-memory cache of disk blocks.

### 5.1 How It Works

**First read (cold cache):**
```
1. read() syscall
2. VFS checks page cache → MISS
3. Filesystem driver issues block I/O request
4. Block read from disk (SSD: ~0.1ms, HDD: ~10ms)
5. Data copied to page cache
6. Data copied from page cache to user buffer
7. Return to user space
```

**Second read (hot cache):**
```
1. read() syscall
2. VFS checks page cache → HIT
3. Data copied directly from page cache to user buffer
4. Return to user space (microseconds!)
```

### 5.2 Benefits

| Scenario | Without Cache | With Cache |
|----------|---------------|------------|
| Read same file twice | 2 disk I/Os | 1 disk I/O |
| Multiple processes reading same file | N disk I/Os | 1 disk I/O |
| Sequential reads | Multiple seeks | Pre-fetch optimization |

### 5.3 Cache Eviction

When memory is tight, kernel uses **LRU** (Least Recently Used) to evict pages:
- Pages not accessed recently are freed
- Dirty pages (modified) are written back first
- Clean pages (read-only) are simply dropped

### 5.4 Verification

You can verify page cache behavior by:

**Cold cache (force eviction):**
```bash
# Linux: echo 3 > /proc/sys/vm/drop_caches
# macOS: purge
time ./reader  # Slower (disk I/O)
```

**Hot cache:**
```bash
time ./reader  # Fast (memory)
time ./reader  # Fast (memory)
```

---

## 6. Kernel Layers Summary

Complete journey of `read()` syscall:

```
┌─────────────────────────────────────────┐
│   User Space (C++ std::ifstream)        │
│   - std::getline(file, line)            │
└────────────────┬────────────────────────┘
                 │ system call
                 ↓
┌─────────────────────────────────────────┐
│   Syscall Interface                      │
│   - Validate parameters                  │
│   - Switch to kernel mode                │
└────────────────┬────────────────────────┘
                 ↓
┌─────────────────────────────────────────┐
│   VFS Layer                              │
│   - Dereference fd → inode               │
│   - Check permissions                    │
│   - Dispatch to filesystem               │
└────────────────┬────────────────────────┘
                 ↓
┌─────────────────────────────────────────┐
│   Filesystem Driver (APFS/ext4/btrfs)    │
│   - Translate file offset → block number │
│   - Check page cache                     │
└────────────────┬────────────────────────┘
                 ↓
         ┌──────┴────────┐
         │                │
     Cache HIT        Cache MISS
         │                │
         ↓                ↓
   ┌─────────┐    ┌─────────────┐
   │  Page   │    │   Block I/O  │
   │  Cache  │    │   Scheduler  │
   └────┬────┘    └──────┬───────┘
        │                │
        │                ↓
        │         ┌─────────────┐
        │         │   Device    │
        │         │   Driver    │
        │         │ (SSD/HDD)   │
        │         └──────┬───────┘
        │                │
        │                ↓ (disk read)
        │         ┌─────────────┐
        │         │ Page Cache  │
        │         │  (store)    │
        └─────────┴──────┬───────┘
                         ↓
                  ┌─────────────┐
                  │ Copy to     │
                  │ user buffer │
                  └──────┬──────┘
                         ↓
                  Return to user space
```

---

## 7. Performance Implications

### 7.1 Why File I/O is "Slow"

| Operation | Latency |
|-----------|---------|
| L1 cache access | ~1 ns |
| RAM access | ~100 ns |
| SSD read (page cache miss) | ~100 μs |
| HDD read (page cache miss) | ~10 ms |
| Network read (NFS) | ~1 ms - 100 ms |

**Takeaway:** Page cache is critical! A cache hit is 1000x faster than SSD, 100,000x faster than HDD.

### 7.2 Database Implications

This is why databases implement their own buffer pools (Lab 3):
- **Control:** Choose what to keep in memory (hot data, indexes)
- **Avoid double caching:** OS page cache + DB buffer pool is redundant
- **Predictability:** Know exactly when disk I/O happens

PostgreSQL uses:
- `shared_buffers`: Configurable buffer pool (default 128 MB)
- Direct I/O or `O_DIRECT` flag to bypass OS page cache
- Custom eviction policy (ClockSweep - see Lab 3)

---

## 8. macOS vs Linux Differences

### 8.1 System Call Tracing

| Tool | Linux | macOS |
|------|-------|-------|
| strace | ✅ Available | ❌ Not available |
| dtruss | ❌ Not available | ✅ Available (requires sudo, blocked by SIP) |
| dtrace | Limited | ✅ Available (SIP restrictions) |

**Our environment:** macOS with SIP enabled, so detailed syscall tracing is blocked for security.

### 8.2 Filesystem Differences

| Feature | Linux (ext4/btrfs) | macOS (APFS) |
|---------|-------------------|--------------|
| Inode structure | Traditional Unix inode | APFS object with B-Tree index |
| Journaling | ext4 journal / btrfs CoW | APFS CoW (Copy-on-Write) |
| Encryption | dm-crypt | Native FileVault |
| Snapshots | btrfs snapshots | APFS snapshots |

**Both** expose the same POSIX syscall interface through VFS, so our C++ code is portable!

---

## 9. Key Insights

### 9.1 Syscall Overhead
- Each syscall requires **context switch** (user mode ↔ kernel mode)
- Cost: ~100-500 nanoseconds
- Modern iostreams use buffering to minimize syscalls (read 4KB at once, not byte-by-byte)

### 9.2 Inode vs Filename
- **Inode** is the canonical identifier (kernel uses this internally)
- **Filename** is just a directory entry mapping
- Multiple filenames can point to same inode (hard links)
- File descriptor is a per-process handle to an inode

### 9.3 VFS Abstraction
- Makes filesystem implementation transparent to applications
- Enables network filesystems (NFS, SMB) to work with same syscalls
- User-space filesystems (FUSE) use same interface

### 9.4 Page Cache
- Dramatically speeds up repeated reads
- Shared across all processes
- Managed by kernel LRU eviction
- Databases often bypass it for predictability

---

## 10. Verification Commands

### Check inode number:
```bash
$ ls -i test.txt
70910287 test.txt
```

### Get detailed metadata:
```bash
$ stat test.txt
Inode: 70910287
Size: 154 bytes
Blocks: 8
Permissions: 100644
```

### View open file descriptors (while program runs with sleep):
```bash
$ lsof -p <PID>
```

### Trace syscalls (Linux only):
```bash
$ strace -e trace=openat,read,close,fstat,mmap ./reader
```

### Trace syscalls (macOS with SIP disabled):
```bash
$ sudo dtruss -t open,read,close,fstat ./reader
```

---

## 11. Conclusion

This lab demonstrates the complete journey from high-level C++ file I/O to low-level kernel operations:

1. ✅ **std::ifstream** translates to `open()`, `read()`, `close()` syscalls
2. ✅ **Inode** is the kernel's internal file representation
3. ✅ **VFS** provides a uniform interface across different filesystems
4. ✅ **Page cache** dramatically improves read performance
5. ✅ **File descriptor** is a per-process handle to an inode

Understanding this syscall boundary is essential for database systems, which need fine-grained control over disk I/O and caching (as we'll implement in Lab 3's ClockSweep buffer pool).

---

## References

- UNIX System Calls: `man 2 open`, `man 2 read`, `man 2 close`, `man 2 fstat`
- Linux VFS: `Documentation/filesystems/vfs.txt` in kernel source
- macOS System Calls: `man syscall`
- PostgreSQL Buffer Manager: `src/backend/storage/buffer/README`
