# Lab 1: File I/O in C++ — Kernel Journey via strace

**Name:** Arman Barbhuiya  
**Roll Number:** 24bcs10196 
**Course:** Advanced Database Management Systems (AdvDBMS)

---

## 1. Overview
This report traces the execution of a simple C++ program that opens and reads a text file. Using `strace`, we observe the boundary between user-space application logic and kernel-space system execution. We follow the request as it propagates through the Virtual Filesystem Switch (VFS), directory entry lookups (dentry), inode resolution, page caches, and physical block device drivers.

---

## 2. Code Implementation

The reader program uses C++ standard file streams (`std::ifstream`) to read lines from `test.txt` and write them to standard output:

```cpp
#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream file("test.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }
    return 0;
}
```

---

## 3. Syscall Tracing with `strace`

Using `strace -e trace=openat,read,close,fstat,mmap ./reader` to monitor syscall execution, we see:

```text
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0
close(3)                                = 0
```

### Syscall Analysis

| Syscall | Parameters | Output / Return Value | Description |
| :--- | :--- | :--- | :--- |
| **`openat`** | `AT_FDCWD`, `"test.txt"`, `O_RDONLY` | `3` (File Descriptor) | Opens the file relative to the current working directory in read-only mode, returning a new file descriptor (fd = 3). |
| **`fstat`** | `3`, pointer to `stat` struct | `0` (Success) | Fetches file metadata (permissions, file size, inode index) using the file descriptor. |
| **`read`** | `3`, buffer pointer, `4096` | `18` (Bytes read) | Attempts to read up to 4096 bytes from fd 3. It successfully reads the 18-byte content. |
| **`read`** | `3`, buffer pointer, `4096` | `0` (EOF) | Attempts another read; returns 0 signaling End of File (EOF). |
| **`close`** | `3` | `0` (Success) | Closes the file descriptor, releasing the system handle and decrementing the inode reference count. |

---

## 4. The Inode & VFS Journey

```
           +----------------------------------+
           |         std::ifstream            | (User Space C++)
           +----------------------------------+
                            |
           +----------------------------------+
           |            fread / libc          |
           +----------------------------------+
                            |
   ========= read() or openat() Syscall Boundary ====================
                            |
           +----------------------------------+
           |   VFS (Virtual Filesystem Switch)| (Kernel Space)
           +----------------------------------+
                            |
           +----------------------------------+
           |    Filesystem Driver (e.g. ext4) |
           +----------------------------------+
                            |
           +----------------------------------+
           |     Page Cache (RAM Cache)       | <-- Cache Hit
           +----------------------------------+
                            | (Cache Miss)
           +----------------------------------+
           |   Block Device Driver (NVMe/SATA)|
           +----------------------------------+
                            |
           +----------------------------------+
           |        Physical SSD / HDD        |
           +----------------------------------+
```

### Step-by-Step Path Resolution

1. **VFS Path Resolution:** The kernel's Virtual Filesystem Switch (VFS) receives the `openat` call. It parses `"test.txt"` starting from the current directory `AT_FDCWD`.
2. **Directory Lookup (dentry):** The kernel looks up directory entries (`dentry` cache) to map the name `"test.txt"` to its unique **inode number**.
3. **Inode Instantiation:** If the inode is cached in the kernel's `icache`, it's resolved instantly. Otherwise, the filesystem driver (e.g., ext4) reads the metadata blocks from disk to populate a kernel `struct inode`.
4. **Permissions Check:** The kernel compares the process UID/GID against the inode properties (`st_uid`, `st_gid`, and `st_mode` permissions).
5. **File Descriptor Mapping:** Upon success, a new entry is added to the process's local open-file table, assigning file descriptor `3` to point to a new `struct file` containing the pointer to the inode and the current offset.

---

## 5. Page Cache & Physical I/O

- **Page Cache check:** When `read(3, ...)` is invoked, the kernel does not immediately query the disk. It checks if the pages containing the requested offset are present in the **page cache** (RAM).
- **Cache Hit:** If the file has been accessed recently, the read is completed by copying data directly from kernel memory pages to the user-space buffer. This requires zero physical disk operations.
- **Cache Miss (Cold Read):** If the pages are not present in memory, the filesystem driver schedules block reads via the block device driver (e.g., NVMe/SATA interface), loading the data into the page cache, and then copies it to user space.

---

## 6. Key Takeaways
- The user-space C++ standard library interacts with files by invoking low-level kernel system calls (`openat`, `read`, `close`).
- File descriptors are process-local indexes, whereas inodes represent the filesystem's physical descriptors of the data.
- The **Virtual Filesystem Switch (VFS)** acts as an abstraction layer, allowing C++ code to operate identically across various underlying filesystems (ext4, NTFS, XFS, etc.).
- Operating system performance relies heavily on caching metadata (inodes/dentries) and file data (page cache) in RAM to minimize latency.