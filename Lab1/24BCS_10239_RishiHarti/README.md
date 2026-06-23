# Lab 1: C++ File I/O Traced with Strace
**Student:** Rishi Harti  
**Roll Number:** 24BCS10239  

---

## 1. Objective
Understand the complete kernel journey of file I/O operations from high-level C++ standard streams down to Virtual Filesystem (VFS) operations, inode resolution, the Page Cache, and raw hardware block device drivers.

---

## 2. System Architecture: User-space to Physical Disk

When executing a C++ file reader, the application transitions through multiple abstraction layers:

```
+-------------------------------------------------+
|            C++ Application (user-space)         |
|   e.g., std::ifstream -> std::getline()         |
+------------------------+------------------------+
                         |
                         v (Library / libc wrappers)
+-------------------------------------------------+
|               Syscall Interface                 |
|      openat(), fstat(), read(), close()         |
+------------------------+------------------------+
                         |
  [User-Kernel Boundary] | (Trap / Software Interrupt)
                         v
+-------------------------------------------------+
|            VFS (Virtual Filesystem)             |
|   Standardizes operations across filesystems    |
+------------------------+------------------------+
                         |
                         v (Path & Inode Resolution)
+-------------------------------------------------+
|           Filesystem Driver (e.g., ext4)        |
|     Translates logic blocks to disk sectors     |
+------------------------+------------------------+
                         |
                         +---> [ Page Cache (RAM) ] (Hit: bypasses disk)
                         |
                         v (Miss: Block I/O request)
+-------------------------------------------------+
|              Block Device Driver                |
|     Queues and schedules mechanical/SSD read    |
+-------------------------------------------------+
```

---

## 3. The Syscall Journey (Strace Output)

Tracing our compiled C++ binary using `strace`:
```bash
g++ -std=c++17 -o reader main.cpp
strace -e trace=openat,fstat,read,mmap,close ./reader
```

### Observed System Calls:

| Syscall | Context in `main.cpp` | Kernel Action | Result / Output |
| :--- | :--- | :--- | :--- |
| **`openat()`** | `std::ifstream file("test.txt")` | Traverses path to resolve the file name to an inode; allocates an unused file descriptor (FD). | `openat(AT_FDCWD, "test.txt", O_RDONLY) = 3` |
| **`fstat()`** | Internal stream initialization | Retrieves metadata of the file backing FD 3 (file size, block size, inode number). | `fstat(3, {st_mode=S_IFREG|0644, st_size=20, ...}) = 0` |
| **`read()`** | `std::getline(file, line)` | Pulls bytes from the Page Cache or physical disk block into a buffer allocated in user-space memory. | `read(3, "hello from lab 1\n", 8192) = 20` |
| **`close()`** | `file.close()` | Releases FD 3, clean up active page mappings, and decrements inode reference count. | `close(3) = 0` |

---

## 4. Inode & VFS Path Resolution

When `openat(AT_FDCWD, "test.txt", O_RDONLY)` is invoked:
1. **Path Resolution**: The kernel checks `AT_FDCWD` (current working directory of the process). It traverses the directory tree component by component.
2. **Directory Lookup**: Directories are files containing lists of mappings: `filename -> inode_number`. The kernel resolves `"test.txt"` to its unique inode number.
3. **Inode Loading**: The VFS fetches the disk's inode structure (or loads it from the active `icache` in RAM). The inode contains metadata:
   - File size (`st_size`)
   - Permissions and Ownership (`st_mode`, `st_uid`, `st_gid`)
   - Block map pointer array (locations of actual data sectors)
4. **FD Allocation**: The kernel creates a `struct file` entry in the system-wide open file table (holding file offsets and flags) and updates the process's local File Descriptor table, returning descriptor `3`.

---

## 5. The Page Cache Interface

File reads are absorbed by the kernel **Page Cache** to avoid high-latency disk interactions:
- **Cache Hit**: If another process recently accessed `test.txt`, its data pages exist in RAM. The `read()` syscall copies bytes directly from kernel page-cache memory into user-space `std::string` buffer. No physical Disk I/O occurs.
- **Cache Miss**: If pages are cold:
  1. The filesystem driver sends a Block I/O request to the disk.
  2. The disk controller transfers blocks into Page Cache pages via DMA (Direct Memory Access).
  3. The kernel copies data to user-space and updates page flags to active.
- **Read-Ahead**: To optimize performance, the kernel automatically pre-fetches contiguous disk blocks into the cache, guessing sequential file reads before the application explicitly asks.

---

## 6. Verification via `/proc` Filesystem

During application execution (e.g., adding a `sleep()` before closing), we can inspect the active file descriptors:

```bash
# Get PID of running reader
ps aux | grep reader

# List active FDs of the process
ls -la /proc/<PID>/fd/
```

**Expected Output:**
```text
lr-x------ 0 rishi rishi 64 Jun  1 12:05 0 -> /dev/pts/1
lrwx------ 1 rishi rishi 64 Jun  1 12:05 1 -> /dev/pts/1
lrwx------ 1 rishi rishi 64 Jun  1 12:05 2 -> /dev/pts/1
lr-x------ 1 rishi rishi 64 Jun  1 12:05 3 -> /workspace/rishi-harti768/scaler-Adv-DBMS/test.txt
```
FD `3` is resolved as a symlink pointing directly to our target file `test.txt` on disk.
