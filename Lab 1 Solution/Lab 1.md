# Lab Session 1: C++ file I/O traced with strace: inode → VFS → page cache → syscall journey


## Objective

Understand what happens under the hood when a C++ program opens and reads a file: tracing the journey from the user-space standard library down through the system call interface, Virtual Filesystem (VFS) layer, inodes, page cache, and block devices.

---

## Steps

### 1. Write a simple C++ file reader

```cpp
#include <iostream>
#include <fstream>
#include <string>

int main() {
    // Initialize file stream handler
    std::ifstream input_file("test.txt");
    
    // Validate file connection immediately (early exit on failure)
    if (!input_file.is_open()) {
        std::cerr << "[ERROR] Unable to open target file: test.txt\n";
        return 1;
    }
    
    // Process and stream file contents line by line
    std::string current_line;
    while (std::getline(input_file, current_line)) {
        std::cout << current_line << "\n";
    }
    
    return 0;
}

```

Compile the program and seed a test file:

```bash
echo "hello from lab 1" > test.txt
g++ -std=c++17 -o file_reader file_reader.cpp

```

---

### 2. Trace with strace

To observe the system call boundaries interacting directly with the kernel, run the executable wrapped inside `strace`:

```bash
strace -e trace=openat,read,close,fstat,mmap ./file_reader

```

#### Primary System Calls Witnessed

| System Call | Operational Mechanics |
| --- | --- |
| `openat` | Opens a file relative to a directory file descriptor (defaults to `AT_FDCWD` for the current working directory); returns a process-specific file descriptor (fd). |
| `fstat` | Retrieves the backing inode's metadata (file size, permissions, ownership, and tracking timestamps) associated with an open file descriptor. |
| `read` | Copies a specified stream of bytes from an open file descriptor into a designated user-space buffer. |
| `mmap` | Maps files, devices, or runtime shared libraries directly into the process's virtual address space. |
| `close` | Deallocates a file descriptor, clears it from the process table, and decrements the internal kernel inode reference counter. |

#### Condensed Trace Output

```text
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                        = 0    # 0 Bytes read signals EOF (End of File)
close(3)                                 = 0

```

---

### 3. The Inode Journey

When the execution path hits the `openat` system call boundary, the kernel initiates the following procedural operations:

1. **Path Resolution:** Resolves the provided filename string into a strict physical endpoint, walking the target directory tree from `AT_FDCWD` directory entry by directory entry.
2. **Inode Lookup:** Maps the human-readable filename to its canonical inode identification integer. The kernel pulls the structure from the underlying filesystem storage layout (e.g., ext4, XFS, Btrfs) into the system's runtime VFS layer.
3. **Authorization Check:** Cross-references the active process's execution context (UID, GID) against the target inode's access parameters (`st_mode`, `st_uid`, `st_gid`).
4. **File Descriptor Allocation:** Registers an available tracking entry index within the process's private open-file table. This structure maps to a global kernel `struct file`, tracking the current offset index and pointing to the canonical inode.

To view a file's mapped inode details, utilize command-line inspection utilities:

```bash
ls -i test.txt
# Output example: 1234567 test.txt

stat test.txt
# Yields: Inode: 1234567, Links: 1, Size: 18, Blocks: 8

```

---

### 4. Linux Kernel I/O Stack Layers

```text
               User Space (Application Context)
               ┌──────────────────────────────┐
               │      C++ std::ifstream       │
               └──────────────┬───────────────┘
                              ▼
               ┌──────────────────────────────┐
               │    std::fread() / glibc      │
               └──────────────┬───────────────┘
                              │
  ════════════════════════════┼════════════════════════════ System Call Boundary
                              ▼
               ┌──────────────────────────────┐
               │ Virtual Filesystem Switch    │ (VFS Abstract Interface Layer)
               └──────────────┬───────────────┘
                              ▼
               ┌──────────────────────────────┐
               │ Filesystem Driver            │ (Native Layouts: ext4 / Btrfs)
               └──────────────┬───────────────┘
                              ▼
               ┌──────────────────────────────┐
               │ Page Cache Manager           │ (Absorbs read hits directly from RAM)
               └──────────────┬───────────────┘
                              │ (Cache Miss Pathway)
                              ▼
               ┌──────────────────────────────┐
               │ Block Device Driver Engine   │ (NVMe / SATA Communication Protocols)
               └──────────────┬───────────────┘
                              ▼
                 [ Physical Disk Hardware ]

```

* **Page Cache Optimization:** The operating system caches reads in system memory. Subsequent read operations target memory directly, preventing redundant physical disk read operations.
* **Metadata Buffering:** Accessing structural data points with `fstat` is computationally inexpensive because inode properties are managed internally inside the kernel's volatile **inode cache (icache)**.

---

### 5. Runtime Inspection via `/proc`

To view active descriptors allocated to a process dynamically while it executes, add a temporal pause (`std::this_thread::sleep_for`) into the source code and query the virtual `/proc` file environment:

```bash
# Retrieve a list of file descriptor allocations assigned to the target PID
ls -l /proc/<PID>/fd

# Inspect the file descriptor link node to view absolute inode parameters
stat /proc/<PID>/fd/3

```

---

## Key Takeaways

* **System Call Redirection:** Every high-level abstraction loop handled by `std::ifstream::open` relies fundamentally on low-level `openat` system calls to traverse directory entries and secure an active inode.
* **Descriptors vs. Inodes:** A file descriptor is an isolated, integer index lookup tracking state local to a single process. An inode is the operating system's global, unique representation of a distinct object on disk.
* **Boundary Transits:** `strace` maps user-space executions against system hardware entry points, revealing the performance costs of context switching into kernel space.
* **Cache Management:** Volatile page caches protect block storage hardware from repeated, redundant operations; physical disk operations are reserved exclusively for first-time or uncached read paths.
