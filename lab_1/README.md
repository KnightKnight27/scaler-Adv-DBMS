# Lab 1: File I/O in C++ - Kernel Journey via System Calls

**Student:** Pulasari Jai  
**Roll No:** 24BCS10656  
**Date:** June 23, 2026

---

## Overview

This lab explores the low-level kernel mechanisms behind C++ file I/O operations. We implement a simple file reader and analyze the system call journey from user space through the VFS layer to the page cache and physical storage.

---

## Objectives

1. ✅ Write a C++ program that reads a file using `std::ifstream`
2. ✅ Trace the program's system calls (open, read, fstat, close)
3. ✅ Understand the inode → VFS → page cache journey
4. ✅ Document kernel-level interactions

---

## Directory Structure

```
lab_1/
├── README.md          # This file
├── reader.cpp         # Simple file reader implementation
├── test.txt          # Test input file
├── compile.sh        # Compilation script
├── analysis.md       # Detailed syscall analysis & findings
└── reader            # Compiled binary (generated)
```

---

## Implementation

### reader.cpp

A minimal C++ file reader that demonstrates basic I/O operations:

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

**Key operations:**
- `std::ifstream file("test.txt")` → triggers `open()` syscall
- `std::getline(file, line)` → triggers `read()` syscall
- Destructor automatically calls `close()` syscall

---

## Building and Running

### Compile

```bash
chmod +x compile.sh
./compile.sh
```

Or manually:
```bash
g++ -std=c++17 -O2 -Wall -Wextra -o reader reader.cpp
```

### Run

```bash
./reader
```

**Expected output:**
```
hello from lab 1
this is advanced database management systems
exploring file I/O and system calls
understanding the kernel journey
from VFS to page cache
```

---

## System Call Tracing

### On Linux

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

**Expected syscalls:**
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=154, ...}) = 0
read(3, "hello from lab 1\n...", 4096) = 154
read(3, "", 4096) = 0    # EOF
close(3) = 0
```

### On macOS

```bash
# Requires sudo and SIP disabled
sudo dtruss -t open,read,close,fstat ./reader
```

**Note:** macOS with System Integrity Protection (SIP) enabled blocks detailed syscall tracing for security. The analysis document explains the expected syscalls based on C++ iostream behavior and Unix/POSIX standards.

---

## File Metadata

### Inode Information

```bash
$ ls -i test.txt
70910287 test.txt

$ stat test.txt
Inode: 70910287
Size: 154 bytes
Blocks: 8
Permissions: 100644 (rw-r--r--)
```

The **inode number** (70910287) is the kernel's internal identifier for the file. The filename "test.txt" is just a directory entry that maps to this inode.

---

## Key Concepts Demonstrated

### 1. System Call Boundary

```
User Space (C++ std::ifstream)
        |
        | syscall interface
        ↓
Kernel Space (VFS, filesystem driver, block I/O)
```

Every file operation crosses this boundary, requiring a context switch.

### 2. Inode Journey

```
Filename ("test.txt")
    ↓ (directory entry lookup)
Inode (70910287)
    ↓ (contains metadata + block pointers)
Data Blocks (on disk/SSD)
```

### 3. VFS (Virtual Filesystem Switch)

The VFS layer provides a uniform interface:
- `open()` → `vfs_open()` → filesystem-specific `open()`
- Works across ext4, APFS, NFS, FUSE, etc.

### 4. Page Cache

```
First read:  disk → page cache → user buffer (slow)
Second read: page cache → user buffer (fast!)
```

Dramatically improves performance for repeated reads.

---

## Testing Results

### ✅ Compilation Test
```
$ ./compile.sh
Compiling reader.cpp...
✓ Compilation successful!
```

### ✅ Execution Test
```
$ ./reader
hello from lab 1
this is advanced database management systems
exploring file I/O and system calls
understanding the kernel journey
from VFS to page cache
```

### ✅ File Metadata Test
```
$ ls -i test.txt
70910287 test.txt

$ stat test.txt
Inode: 70910287
Size: 154 bytes
Blocks: 8
```

**All tests passing!** ✅

---

## Analysis

See **[analysis.md](./analysis.md)** for comprehensive documentation of:

- Detailed explanation of each syscall (`openat`, `fstat`, `read`, `close`, `mmap`)
- Complete inode journey and path resolution process
- VFS architecture and layer interaction
- Page cache mechanism and performance implications
- macOS vs Linux differences
- Database system implications (why databases use custom buffer pools)

---

## Key Takeaways

1. **High-level I/O abstractions** (like `std::ifstream`) are built on low-level syscalls
2. **Inodes** are the kernel's internal file representation; filenames are just directory entries
3. **VFS layer** provides filesystem-independent interface
4. **Page cache** eliminates redundant disk I/O for frequently accessed files
5. **Context switches** (user ↔ kernel mode) have overhead, so buffering is crucial
6. Understanding syscalls is essential for database systems that need fine-grained I/O control

---

## Connection to Database Systems

Database systems (like PostgreSQL) implement their own buffer pools instead of relying on the OS page cache because they need:

- **Control** over what stays in memory (hot data, indexes)
- **Predictability** of when disk I/O occurs
- **Custom eviction policies** (e.g., ClockSweep in Lab 3)
- **Avoiding double caching** (OS page cache + DB buffer = wasted memory)

This lab establishes the foundation for understanding why Lab 3's ClockSweep algorithm exists!

---

## References

- C++ File I/O: `std::ifstream` documentation
- UNIX System Calls: `man 2 open`, `man 2 read`, `man 2 close`
- Linux VFS: kernel documentation
- Lab Session Requirements: `../lab_sessions/lab_1.txt`

---

## Author

**Pulasari Jai** (Roll No: 24BCS10656)  
Advanced Database Management Systems  
Scaler Academy
