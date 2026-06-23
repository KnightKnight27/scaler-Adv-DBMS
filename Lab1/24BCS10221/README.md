# Lab 1: Linux Low-Level File I/O Tracing (VFS, Page Cache & System Calls)

**Name:** MD Kaif Molla  
**Roll No:** 24BCS10221  
**Subject:** Advanced Database Management Systems (Lab 1)  

---

## 1. Programming-Related Concepts

### File Descriptors (fd)
In Unix-like operating systems, *"everything is a file"*. A **File Descriptor (fd)** is a non-negative integer returned by the kernel to uniquely identify an open file within a process. 
- The OS maintains a private **File Descriptor Table** for each running process.
- The integer value of `fd` acts as an index pointing to an entry in this per-process table.
- Each table entry, in turn, points to a system-wide open file table entry, which stores the current file offset, access permissions, and a pointer to the file's in-memory node (**vnode** or **inode**).
- By convention:
  - `0` is Standard Input (`stdin`)
  - `1` is Standard Output (`stdout`)
  - `2` is Standard Error (`stderr`)
- Any new file opened using `open()` is assigned the lowest unused integer index in the table.

### The `errno` Variable
Standard library wrappers around system calls do not directly return complex error details. Instead, when a system call fails:
- It returns `-1` (or a negative value).
- It sets a thread-local global integer variable called `errno` (defined in `<cerrno>`) to indicate the specific error code.
- Using commands like `errno -l` or calling `strerror(errno)` in C++ resolves this numeric code into a human-readable error description (e.g., `ENOENT` / `2` translates to *"No such file or directory"*).

### Memory Pages
A **page** (or virtual page) is the smallest fixed-length block of virtual memory managed by the Operating System's Memory Management Unit (MMU). 
- On standard modern Linux architectures, the page size is typically **4 KB** (4096 bytes).
- Virtual memory, physical memory (RAM), and disk sectors are all structured around these page boundaries to simplify address translation, pagination, and file system block transfers.

---

## 2. Low-Level System Call Wrappers

Standard C/C++ libraries provide low-level wrappers that match the kernel's actual entry-point system calls:

1. **`open(const char* pathname, int flags, ...)`**
   - Allocates a file descriptor and sets up kernel-level tracking for the file specified by `pathname`.
   - `flags` control the opening mode (e.g., `O_RDONLY` for read-only, `O_WRONLY` for write-only, `O_APPEND` to move the file offset pointer to the end of the file before each write).

2. **`read(int fd, void* buf, size_t count)`**
   - Attempts to read up to `count` bytes from the file referenced by `fd` into the memory buffer starting at `buf`.
   - Returns the actual number of bytes read (which can be less than `count` if EOF is reached), or `-1` on error.

3. **`write(int fd, const void* buf, size_t count)`**
   - Writes `count` bytes from the memory buffer `buf` to the file referenced by `fd`.
   - Returns the actual number of bytes written, or `-1` on error.

4. **`close(int fd)`**
   - Removes the entry for `fd` from the process's file descriptor table, decrements the kernel's open count for the file, and releases OS resources.

### Why is `close()` Important?
1. **Flushing Buffers:** System level `write()` calls write data to the OS page cache rather than the physical disk. While calling `close()` does not guarantee immediate physical disk serialization (unlike `fsync`), it releases the user-space and kernel buffer mappings, initiating write-back tasks.
2. **Resource Management:** Operating systems impose a strict limit on the number of open file descriptors a single process can hold (viewable via `ulimit -n`). Failing to close files leads to **File Descriptor Leaks**, causing subsequent `open()` calls to fail with `EMFILE` (Too many open files).
3. **Locks and File Integrity:** Some operating systems or concurrent processes check lock counts associated with the open descriptor. Leaving files open can prevent other processes from gaining write access or cleanly unmounting directories.

---

## 3. Storage Hierarchy: RAM vs. Disk I/O

Reading data from physical disks (especially Hard Disk Drives - HDDs) is orders of magnitude slower than reading from RAM. 

### Hardware Physics

* **RAM (Random Access Memory):**
  - Built entirely using integrated silicon circuits (semiconductors).
  - Uses transistors and tiny capacitors (DRAM) that store electrical state.
  - Accessing any address takes a fixed, negligible amount of time because it involves only routing electrical signals through wire matrices.

* **HDDs (Hard Disk Drives):**
  - Rely on electromechanical physics. 
  - Consist of magnetic platters spinning at high speeds (e.g., 7200 RPM) and a mechanical read/write arm.
  - To read/write a block, the platter must spin so that the correct track is under the read/write head (**rotational latency**), and the arm must move radially to the correct track (**seek time**). 
  - These physical mechanics are extremely slow compared to electronic speeds.

* **SSDs (Solid State Drives):**
  - Use NAND flash memory. While they eliminate mechanical movements, they are still limited by controller latencies, page/block erasure cycles, and connection bus speeds (SATA/NVMe), making them faster than HDDs but still significantly slower than system RAM.

### Performance Latency Gap
According to standard CPU latency metrics:
- **L1 Cache access:** ~1 ns
- **RAM access:** ~100 ns
- **SSD Sequential read:** ~100,000 ns (0.1 ms)
- **HDD Seek/Random access:** ~10,000,000 ns (10 ms)

**Comparison Multiplier:**
- **RAM vs. Disk (Sequential):** RAM access is roughly **6x to 10x faster** than sequential disk reads.
- **RAM vs. Disk (Random Access):** RAM access is roughly **100,000x faster** than mechanical random seek times.

---

## 4. The Linux File I/O Pipeline & Page Cache

To bridge the latency gap between RAM and disk, the Linux kernel employs a **Page Cache** layer.

```
+-------------------------------------------------+
|               User Process                      |
+-------------------------------------------------+
                        |  (System Call: read/write)
                        v
+-------------------------------------------------+
|         Virtual File System (VFS)               |
+-------------------------------------------------+
                        |
                        v
+-------------------------------------------------+
|         Page Cache (RAM Cache, 4KB Pages)       |
|  [Page 1: Clean] [Page 2: Dirty] [Page 3: Clean]|
+-------------------------------------------------+
             |                        ^
 (Cache Miss:|                     (Page| (Asynchronous
  Read Block)|                     Fault|  Flush: pdflush/flusher)
             v                        |
+-------------------------------------------------+
|         Block Device Driver & I/O Scheduler      |
+-------------------------------------------------+
                        |
                        v
+-------------------------------------------------+
|               Physical Disk (Storage)           |
+-------------------------------------------------+
```

### The Read Path Journey
When a process executes a `read(fd, buf, count)` system call:
1. **VFS Resolution:** The call traverses the **Virtual File System (VFS)** layer, which identifies the file's mount point and calls the underlying file-system-specific read operations.
2. **Page Cache Lookup:** The OS computes which page offset the requested file block resides in, and checks the **Page Cache** (a hash table containing pointers to physical RAM pages mapped to the file's inode).
3. **Cache Hit:** If the file pages are already in RAM, the kernel copies the data directly from the page cache into the user-space buffer `buf`. No disk I/O occurs.
4. **Cache Miss & Page Fault:** If the pages are not in memory, a cache miss occurs. The kernel allocates a physical page in the page cache, sends an asynchronous I/O request to the disk controller to read the blocks, puts the reading thread to sleep, and wakes it up once the data is read into the page cache page. Finally, the data is copied to the user-space buffer.

*Note on page mappings:* In standard I/O, the kernel copies data from the page cache to user-space buffers. For memory-mapped I/O (`mmap`), the user process maps its virtual memory pages directly to the kernel page cache pages, bypassing the second copy step.

### The Write Path Journey (Buffered Writing)
When a process executes a `write(fd, buf, count)` system call:
1. **Buffered Write:** The data from the user-space buffer `buf` is copied directly into the kernel's **Page Cache** pages.
2. **Dirty Page Marking:** The kernel marks these modified pages in the Page Cache as **dirty** (i.e., their contents are newer than the copy stored on physical disk). The system call then returns immediately, giving the user process a high-speed "successful" write confirmation.
3. **Asynchronous Serialization (Flushing):** The data is flushed to the disk asynchronously by the kernel's background flusher threads (such as `pdflush` or `writeback`), which wake up periodically or when the proportion of dirty pages in RAM exceeds a threshold (`vm.dirty_background_ratio`).
4. **Explicit Syncing:** A program can force a synchronous write to physical disk by calling `fsync(fd)` or `fdatasync(fd)`. Closing the file with `close(fd)` cleans up the file descriptor registry, and the kernel schedules a write-back of any remaining dirty pages.

---

## 5. Compilation and Verification

A `makefile` is provided in this directory to simplify building and running the demonstration.

### Files Included:
- `main.cpp`: A modular C++ program demonstrating system calls (`open`, `read`, `write`, `close`) and detailed `strerror(errno)` reporting.
- `file.txt`: Initial text file containing 5 lines of `"abcd"`.
- `makefile`: Compiler rules.

### How to Compile and Run:
To build the executable and execute it:
```bash
# Compile the main.cpp file
make

# Run the compiled binary
make run
```

### To Clean Build Artifacts:
```bash
make clean
```
