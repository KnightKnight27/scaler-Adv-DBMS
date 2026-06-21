<div align="center">

# 📁 Lab Session 1: File I/O in C++ — Kernel Journey via strace
### Deep Dive from C++ Streams to Linux Kernel System Calls

[![C++](https://img.shields.io/badge/C++-00599C?style=for-the-badge&logo=cplusplus&logoColor=white)](https://isocpp.org/)
[![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.kernel.org/)

</div>

---

## 👨‍🎓 Student Details
- **Name:** Siddhant Prasad
- **Roll Number:** 24BCS10255

---

## 🎯 Objective
Understand what happens under the hood when a C++ program opens and reads a file: from the standard library stream layer down to the Virtual Filesystem (VFS), inodes, system call tracing, and kernel interactions.

---

## 🛠️ Step-by-Step Walkthrough

### 1. Write a simple C++ file reader
We write a standard C++ reader utilizing `std::ifstream` to read content line by line.

Code implementation in [reader.cpp](file:///c:/Users/Siddhant/OneDrive/Desktop/scaler-Adv-DBMS/Lab_1/reader.cpp):
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

#### Compile and Create a Test File
Create the test file `test.txt` and compile the program:
```bash
echo "hello from lab 1" > test.txt
g++ -std=c++17 -O2 reader.cpp -o reader
```

---

### 2. Trace with `strace`
To intercept the system call boundary between user-space and kernel-space, run `strace` filtering the critical file-related system calls:
```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

#### Key System Calls Observed
| Syscall | Purpose | Key Parameters | Return Value (Success / Fail) |
| :--- | :--- | :--- | :--- |
| `openat()` | Opens the target file relative to a directory fd | `dirfd`, `pathname`, `flags` | File Descriptor `(>= 0)` / `-1` |
| `fstat()` | Fetches inode metadata (size, permissions, links) | `fd`, `statbuf` | `0` / `-1` |
| `read()` | Reads bytes from the fd into a user-space buffer | `fd`, `buf`, `count` | Bytes read `(> 0)`, EOF `(0)` / `-1` |
| `mmap()` | Maps files/devices into process memory space | `addr`, `len`, `prot`, `flags`, `fd` | Mapped address / `MAP_FAILED` |
| `close()` | Closes the file descriptor, decrementing inode ref count | `fd` | `0` / `-1` |

#### Full Trace (Condensed Example)
```text
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0    # EOF
close(3)                                = 0
```

---

### 3. The Inode Journey
When `openat` is invoked, the kernel performs the following sequence:
1. **Path Resolution**: Walks the directory tree component by component starting from the current working directory (`AT_FDCWD`).
2. **Inode Lookup**: Each directory entry maps a filename to an **inode number**. The kernel fetches the inode from disk/memory structures via the Virtual Filesystem (VFS).
3. **Permission Check**: Compares the process's effective UID/GID against the inode's permissions (`st_uid`, `st_gid`, `st_mode`).
4. **File Descriptor Allocation**: Allocates an integer index (e.g., `3`) in the process's private open-file table pointing to a kernel-level `struct file`. This structure holds the current read/write offset and points to the inode.

To query the inode number and status of a file:
```bash
ls -i test.txt
# e.g.: 1234567 test.txt

stat test.txt
# Output details: Inode: 1234567, Links: 1, Size: 18, Blocks: 8
```

---

### 4. Kernel Layers Involved

```mermaid
graph TD
    A[C++ std::ifstream] --> B[fread / libc]
    B --> C["read() / openat() syscall"]
    subgraph Kernel Space
        C --> D[VFS - Virtual Filesystem Switch]
        D --> E[Filesystem Driver - ext4 / btrfs]
        E --> F{Page Cache Hit?}
        F -- Yes --> G[Return data from RAM]
        F -- No --> H[Block Device Driver]
        H --> I[Physical Disk SSD/HDD]
    )
```

- **Page Cache**: Repeated reads of the same file avoid disk access entirely, serving data directly from RAM.
- **Inode Cache (icache)**: Inode metadata lookups for `fstat` are extremely fast as the kernel caches hot inodes in memory.

---

### 5. Verification with `/proc`
To inspect active file descriptors at runtime:
1. Add a brief pause/sleep inside the program.
2. Check the active directory of descriptors:
   ```bash
   ls -l /proc/<PID>/fd
   ```
3. Show the inode backing descriptor 3:
   ```bash
   stat /proc/<PID>/fd/3
   ```

---

## 📝 Key Takeaways
- Every `std::ifstream` open ultimately delegates to the low-level `openat` system call.
- The **File Descriptor** is a process-specific integer handle, whereas the **Inode** is the kernel's persistent, canonical representation of the physical file.
- Tools like `strace` expose the exact boundary between user-space libraries and kernel-space execution.
- The **Page Cache** absorbs repeated reads, ensuring only cold reads or dirty write flushes hit physical storage.
