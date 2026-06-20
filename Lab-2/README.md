# Lab 2 - File I/O in C++ — Kernel Journey via strace

**Course:** Advanced DBMS  
**Author:** Vedanshu Nishad (24BCS10285)

---

## Objective

Understand what happens under the hood when C++ opens and reads a file: from the syscall layer down to inodes, the VFS, and kernel interactions.

---

## Background: The File I/O Stack

```
C++ std::ifstream / std::getline
        ↓
libc (buffered I/O) / libstdc++
        ↓
User/Kernel Boundary
        ↓
Linux Kernel Syscalls (openat, read, fstat, close)
        ↓
VFS (Virtual Filesystem Switch)
        ↓
Filesystem Driver (ext4 / btrfs / etc)
        ↓
Page Cache (in-memory buffer)
        ↓
Disk I/O (if page cache miss)
```

Understanding this hierarchy helps us see why DBMS systems use memory-mapped I/O (mmap) and custom buffer pools rather than standard C I/O.

---

## Files in This Lab

| File | Purpose |
|------|---------|
| `reader.cpp` | Simple C++ file reader using std::ifstream |
| `test.txt` | Test data file (4 lines, ~150 bytes) |
| `CMakeLists.txt` | Build configuration |
| `README.md` | This document |

---

## Build & Run

```bash
mkdir -p build
cd build
cmake ..
make

# Run without strace
./reader

# Run WITH strace (see syscalls)
strace -e trace=openat,read,close,fstat ./reader

# Detailed trace with all syscalls
strace ./reader

# Trace specific file operations
strace -e trace=file ./reader
```

---

## Execution & Syscall Analysis

### Without strace:
```
[INFO] Opening file with std::ifstream...
[SUCCESS] File opened
[INFO] Reading file line by line...
[LINE 1] hello from lab 2 - file i/o demonstration
[LINE 2] inode -> VFS -> page cache journey
[LINE 3] this is a test file for strace analysis
[LINE 4] kernel layers involved in file operations
[INFO] Read 4 lines
[INFO] Closing file...
[SUCCESS] File closed
```

### With strace (key syscalls):
```
openat(AT_FDCWD, "test.txt", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=153, ...}) = 0
read(3, "hello from lab 2\n...", 4096) = 153
read(3, "", 4096) = 0
close(3) = 0
```

---

## The Syscall Journey

### 1. **openat() - Path Resolution → Inode Lookup**
```
openat(AT_FDCWD, "test.txt", O_RDONLY|O_CLOEXEC) = 3
```

**What happens:**
- `AT_FDCWD`: Relative to current working directory
- Kernel walks the directory tree component-by-component
- For each component, performs inode lookup in filesystem metadata
- Compares process UID/GID against inode permissions (st_uid, st_gid, st_mode)
- Allocates a file descriptor (fd=3) in the process's open-file table
- Returns fd pointing to `struct file` with offset=0 and pointer to inode

**Kernel Code Path:**
```
sys_openat()
  → getname() [copy path from user space]
  → do_filp_open()
    → path_openat()
      → walk_component() [for each path component]
        → inode_lookup()
          → filesystem->lookup() [e.g., ext4_lookup()]
      → may_open() [permission check against inode->i_uid, i_gid, i_mode]
      → allocate_fd() [add to process->files->fdt]
```

Check inode number:
```bash
ls -i test.txt
# Output: 1234567 test.txt

stat test.txt
# Shows: Inode: 1234567, Size: 153, Blocks: 8
```

---

### 2. **fstat() - Metadata Fetch**
```
fstat(3, {st_mode=S_IFREG|0644, st_size=153, ...}) = 0
```

**What happens:**
- Kernel retrieves inode metadata via fd
- Populates `struct stat` with: size, permissions, times (atime, mtime, ctime), blocks
- **Key insight:** std::ifstream calls fstat internally to determine file size for buffering

**Why important for DBMS:**
- PostgreSQL uses fstat to determine buffer cache strategy
- SQLite checks file size to decide page allocation

---

### 3. **read() - Page Cache Lookup**
```
read(3, "hello from lab...", 4096) = 153
```

**What happens (simplified):**
- Kernel extracts offset from `struct file->f_pos`
- Checks page cache: "Is page at offset 0 already in memory?"
  - **HIT:** Return cached page (no disk I/O)
  - **MISS:** Allocate page, trigger disk I/O, add to page cache
- Copies bytes from page cache to user buffer
- Updates `f_pos += 153`

**Kernel Code Path:**
```
sys_read()
  → vfs_read()
    → do_iter_read()
      → filesystem->read_iter() [e.g., generic_file_read_iter()]
        → page_cache_sync_readahead()
          → read_pages()
            → bio_submit() [disk I/O]
            → wait_on_page_locked()
        → copy_page_to_iter() [page cache → user buffer]
        → file->f_pos += bytes_read
```

**Page Cache Behavior:**
- First read: Page 0 fetched from disk (HIT page cache next time)
- Second read: Served from page cache (NO disk I/O)
- Kernel automatically manages eviction via LRU

---

### 4. **read() - EOF**
```
read(3, "", 4096) = 0
```

**What happens:**
- Kernel checks: is offset >= file size?
- Returns 0 bytes (EOF indicator)
- C++ std::getline detects EOF and stops loop

---

### 5. **close() - Resource Cleanup**
```
close(3) = 0
```

**What happens:**
- Kernel decrements inode reference count
- Removes fd from process's open-file table
- Flushes any dirty pages (if write mode)
- Frees `struct file` memory

---

## Architecture Insights

### Multiple Levels of Buffering:
1. **Application:** C++ std::ifstream has its own buffer
2. **Library:** libc buffers (stdio)
3. **Kernel:** Page cache (shared across all processes)

### Why This Matters for DBMS:
- **PostgreSQL:** Disables stdio buffering, uses shared_buffers (own page cache)
- **SQLite:** Uses mmap to bypass read() syscalls entirely
- **RocksDB:** Bypasses page cache with Direct I/O for write-heavy workloads

### Performance Implications:
- Multiple buffer copies = wasted CPU
- Page cache is great for reads, but writers must manage carefully
- DBMS systems implement their own buffer pools for better control

---

## Measurement: Multiple Reads

Create `measure.cpp` to observe page cache behavior:
```cpp
#include <iostream>
#include <fstream>
#include <chrono>

int main() {
    const char* filename = "test.txt";
    
    for (int i = 1; i <= 3; ++i) {
        auto start = std::chrono::high_resolution_clock::now();
        
        std::ifstream file(filename);
        std::string line;
        int count = 0;
        while (std::getline(file, line)) count++;
        file.close();
        
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        
        std::cout << "Read #" << i << ": " << duration.count() << " us (" << count << " lines)\n";
    }
    return 0;
}
```

**Typical Output:**
```
Read #1: 125 us (first read - disk I/O)
Read #2: 15 us (page cache HIT)
Read #3: 12 us (page cache HIT)
```

The huge difference shows page cache effectiveness!

---

## Key Learnings

1. **Syscalls are Expensive:** Every read/write crosses user/kernel boundary
2. **Page Cache Reduces I/O:** Repeated accesses served from memory
3. **Multiple Buffer Layers:** Application, library, kernel all buffer independently
4. **DBMS Bypass This:** Sophisticated systems like PostgreSQL implement their own buffer pools to:
   - Control eviction policy (not just LRU)
   - Manage durability (fsync timing)
   - Support specialized workloads (OLTP vs OLAP)
   - Reduce buffer copy overhead

5. **strace is Powerful:** Reveals exactly what kernel operations your code triggers

---

## References

- Linux Kernel Documentation: fs/page_cache.c
- Linux VFS Documentation: fs/read_write.c
- PostgreSQL Source: src/backend/storage/file/fd.c
- "Linux Kernel Development" by Robert Love - Chapters on VFS and Page Cache

---

## Follow-up Experiments

1. **Measure Impact of Large Files:** Create a 1GB test file and observe page cache invalidation
2. **Compare with mmap:** Implement same reader using mmap and compare performance
3. **Trace Write Operations:** Create a writer and trace write() syscalls + fsync()
4. **Concurrent Readers:** Run multiple instances to observe shared page cache behavior

