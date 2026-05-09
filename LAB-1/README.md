# FileModifier Lab - Learning Journey

## Overview
This lab demonstrates how a C++ program interacts with the file system at the kernel level using system calls. The `FileModifier.cpp` program reads a file, prints its content, and overwrites it with new text—all while leveraging low-level kernel mechanisms.

## Program Description
**FileModifier.cpp** performs the following operations:
1. Opens `testFile.txt` using the `open()` system call
2. Reads the current file content using `read()`
3. Displays the original content to stdout
4. Seeks to the beginning using `lseek()`
5. Writes new text using `write()`
6. Syncs changes to disk using `fsync()`
7. Closes the file using `close()`

## Key Concepts Learned

### 1. System Calls (Syscalls)
**Definition**: A system call is a request from a user-space program to the kernel to perform a privileged operation that requires direct hardware access.

**Why they exist**: User programs cannot directly access hardware (disk, network, etc.) due to CPU protection mechanisms. Syscalls are the controlled interface between applications and the kernel.

**Syscalls used in FileModifier**:
- `open()` - Opens or creates a file
- `read()` - Reads data from a file
- `write()` - Writes data to a file
- `lseek()` - Moves the file pointer to a specific position
- `fsync()` - Ensures data is written to disk
- `close()` - Closes a file descriptor

**Cost**: Syscalls are expensive because they involve a context switch from user-space to kernel-space and back.

---

### 2. File Descriptor (fd)
**Definition**: A file descriptor is an integer that the kernel assigns to represent an open file, stream, or resource.

**How it works**:
- The kernel maintains a **file descriptor table** for each process
- When you `open()` a file, the kernel returns an integer (typically 3, 4, 5, ...)
- Standard file descriptors (always available):
  - `0` = stdin (keyboard)
  - `1` = stdout (terminal/console)
  - `2` = stderr (error output)

**In FileModifier**:
```cpp
int fd = open("testFile.txt", O_RDWR | O_CREAT, 0644);  // Returns 3
read(fd, buffer, sizeof(buffer) - 1);                    // Uses fd 3
write(fd, newText, strlen(newText));                     // Uses fd 3
close(fd);                                               // Closes fd 3
```

**The key insight**: The integer `fd` itself has no meaning—it's just a token. The kernel maintains the mapping: `fd 3 → /path/to/testFile.txt`. When you pass `fd` to functions like `read()` or `write()`, those functions ask the kernel, "What file does fd 3 refer to?" and the kernel looks it up.

---

### 3. Strace (System Call Trace)
**Definition**: `strace` is a debugging tool that intercepts and logs all system calls made by a process and their results.

**Usage**:
```bash
strace ./FileModifier
```

**What it shows**:
- Every syscall the program makes
- Arguments passed to each syscall
- Return values and error codes
- File descriptors being used
- Memory operations (mmap, mprotect)
- Process lifecycle (exec, exit)

**Example from our run**:
```
openat(AT_FDCWD, "testFile.txt", O_RDWR|O_CREAT, 0644) = 3
read(3, "", 1023) = 0
write(1, "Original content of file:\n", 26) = 26
lseek(3, 0, SEEK_SET) = 0
write(3, "The CPP file has modified this f"..., 70) = 70
fsync(3) = 0
close(3) = 0
```

**Why it matters**: `strace` reveals exactly what your code is doing at the kernel level—invaluable for debugging file I/O, understanding performance, and learning how the OS works.

---

### 4. Inode (Index Node)
**Definition**: An inode is a data structure on disk that stores metadata about a file and pointers to where the file's actual data is stored.

**What an inode contains**:
- Owner (UID) and group (GID)
- File permissions (rwx for user/group/other)
- Timestamps (created, modified, accessed)
- File size
- Number of hard links
- **Block pointers** - which disk blocks store the file data

**Inode relationship to file descriptor**:
```
File Descriptor (fd 3)
    ↓
Kernel's internal file table
    ↓
Inode (e.g., inode #12345)
    ↓
Disk blocks [1024, 1025, 1026, ...]
```

**Key point**: A file's name is just a link to an inode. The inode number uniquely identifies a file on the filesystem. You can have multiple names pointing to the same inode (hard links), but only one inode per file.

---

### 5. Block
**Definition**: A block is the minimum unit of disk space that can be allocated. Typical block size is 4KB (4096 bytes).

**How files are stored**:
- Files don't occupy contiguous disk space
- Instead, a file is spread across multiple blocks
- The inode contains pointers to these blocks
- Blocks can be scattered anywhere on the disk

**Example**:
```
File: testFile.txt (8KB)
Inode says: data is in blocks [1234, 1235]
Block 1234: first 4KB of file data
Block 1235: second 4KB of file data
```

**Why blocks matter**:
- Efficient disk usage (no fragmentation at allocation time)
- Allows file systems to organize and track storage
- Affects performance (multiple disk seeks needed to read non-contiguous blocks)

---

### 6. Page
**Definition**: A page is the minimum unit of memory that the OS manages. Typical page size is 4KB (same as block size, usually).

**Purpose**: The kernel uses a **page cache** to store frequently accessed disk blocks in RAM.

**How it works in FileModifier**:
1. `read(fd, buffer, ...)` is called
2. Kernel checks: Is the data already in a page in RAM?
   - **Yes**: Copy from page to your buffer (fast, microseconds)
   - **No**: Trigger a disk read (slow, milliseconds)
3. Disk driver fetches the block from disk into a page
4. Kernel copies data from page to your buffer

**The hierarchy**:
```
Block (disk) ← 1:1 mapping → Page (RAM)
```

When you read a file, the kernel loads disk blocks into RAM pages for fast repeated access.

---

### 7. Driver
**Definition**: A driver is kernel-level software that controls specific hardware and implements the protocol for communicating with it.

**Disk driver role**:
- Translates high-level kernel requests ("read block 1234") into hardware commands
- Handles low-level hardware details (which disk, which head, which track)
- Manages interrupts and error handling
- Coordinates with DMA controller

**In FileModifier's flow**:
```
your program: read(fd, ...)
    ↓
kernel page cache layer
    ↓
(block not in cache)
    ↓
disk driver
    ↓
(talks to disk hardware)
```

**Why drivers matter**: They abstract hardware complexity. You don't need to know how the specific disk brand works; the driver handles it.

---

### 8. DMA (Direct Memory Access)
**Definition**: DMA is a hardware feature that allows peripherals (like disk controllers) to copy data directly into RAM without involving the CPU.

**Why DMA exists**:
- Copying data byte-by-byte with the CPU is slow
- CPU could be doing other work
- DMA is much faster for large data transfers

**How it works**:
1. Kernel tells DMA controller: "Fetch disk block 1234 into RAM page at address 0x12345000"
2. DMA controller performs the transfer independently
3. When done, DMA controller sends an interrupt to the CPU: "I'm finished!"
4. Kernel resumes and passes data to your program

**In FileModifier**:
```
disk driver setup:
    "DMA controller, copy block 1234 to page 0x12345000"
    ↓
(DMA works without CPU involvement)
    ↓
DMA interrupt: "Transfer complete!"
    ↓
Kernel processes data
```

**Performance impact**: DMA allows the CPU to do other work while data is being transferred from disk—critical for multitasking systems.

---

## How It All Connects

When you call `read(fd, buffer, 1024)`:

```
1. Your program (user-space) makes a syscall
   ↓
2. Kernel intercepts the syscall
   ↓
3. Kernel looks up: fd 3 → inode #12345
   ↓
4. Inode says: data is in blocks [1234, 1235]
   ↓
5. Kernel checks page cache: are blocks 1234, 1235 in RAM?
   - If YES: copy from pages to your buffer (done!)
   - If NO: proceed to step 6
   ↓
6. Kernel tells disk driver: "Fetch block 1234"
   ↓
7. Disk driver sets up DMA: "Transfer block 1234 into page at 0xABCD1000"
   ↓
8. DMA controller copies data from disk to RAM independently
   ↓
9. DMA signals completion via interrupt
   ↓
10. Kernel copies data from page to your buffer
    ↓
11. Syscall returns; your buffer now has file data
```

---

## Compilation & Execution

**Requirements**:
- GCC/G++ compiler
- Linux kernel with standard system calls

**Compile** (Fedora 44):
```bash
sudo dnf install gcc-c++
g++ -std=c++17 -O2 -o FileModifier FileModifier.cpp
```

**Run**:
```bash
./FileModifier
```

**Expected output**:
```
Original content of file:
[original content of testFile.txt]
File successfully modified and saved
```

**Trace system calls**:
```bash
strace ./FileModifier
```

---

## Lessons Learned

1. **Syscalls are the bridge**: User programs cannot directly access hardware; syscalls are the controlled interface.

2. **File descriptors are tokens**: An fd is just an integer; the kernel maintains the actual mapping to files.

3. **The kernel does the heavy lifting**: Behind simple `read()` and `write()` calls, the kernel manages inodes, blocks, pages, DMA, and drivers.

4. **Layering is powerful**: The filesystem abstraction hides complexity (blocks, inodes) while providing a simple interface (file names, file descriptors).

5. **Performance optimization is everywhere**: Page caching, DMA, and drivers all exist to speed up I/O while the CPU does other work.

6. **strace is your friend**: Understanding syscall traces gives insight into what your program is *really* doing at the OS level.

---

## Author
Created for ADBMS LAB-1
Date: May 2026
