# Advanced DBMS Lab 1

## Topic
File I/O, System Calls, and the Storage Journey

**Name:** Om Malviya
**Roll Number:** 24BCS10448
**Course:** Advanced Database Management Systems
**Language:** C++17

## Objective
The goal of this lab is to understand how a simple file operation in a program travels through the C++ runtime, the operating system, the file system, and finally storage. The point is not just to write a file, but to understand what really happens underneath.

## Implementation

### C++ Program
The program uses POSIX `open`/`write`/`read` system calls directly to write data to a file and read it back. This avoids higher-level C++ I/O wrappers so the system call path is as visible as possible.

```cpp
#include <fcntl.h>
#include <unistd.h>

int fd_out = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
ssize_t bytes_written = write(fd_out, content, strlen(content));
fsync(fd_out);
close(fd_out);

int fd_in = open(file_name, O_RDONLY);
ssize_t bytes_read = read(fd_in, buffer, sizeof(buffer) - 1);
close(fd_in);
```

### What the code does
1. Creates a text payload in memory.
2. Opens a file for writing using POSIX `open`.
3. Writes bytes to the OS through the `write` syscall.
4. Calls `fsync` to request durability.
5. Opens the same file for reading.
6. Reads the bytes back into memory.
7. Prints the content so the round trip is visible.

## Code Explanation

`open()` asks the kernel to resolve the pathname, check permissions, and return a file descriptor. The flags `O_WRONLY | O_CREAT | O_TRUNC` mean: write-only, create if not exists, truncate to zero if it does.

`write()` does not mean "write to disk right now." The kernel usually copies the bytes into its page cache first, marks the page dirty, and schedules physical disk flush later. That is why file writes can return before the bytes are permanently on storage.

A successful `write` does not guarantee that data is physically persisted on disk. The data may remain in the kernel page cache and be flushed later. Durability is only guaranteed after an explicit `fsync()`.

`read()` works in the opposite direction. If the data is already in the page cache from the write, the kernel serves it from RAM without touching the disk.

`close()` releases the file descriptor. It does not guarantee a flush; dirty pages may still be written back by the kernel afterward unless `fsync` was called.

## System Call Analysis

Run on Linux with:

```bash
g++ -std=c++17 -o file_io file_io.cpp
strace -e trace=openat,read,write,close,fsync ./file_io
```

The relevant lines from the trace:

```text
openat(AT_FDCWD, "assignment-data.txt", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
write(3, "Advanced DBMS Lab 1\nThis file wa"..., 178) = 178
fsync(3)                                = 0
close(3)                                = 0
openat(AT_FDCWD, "assignment-data.txt", O_RDONLY) = 3
read(3, "Advanced DBMS Lab 1\nThis file wa"..., 1023) = 178
close(3)                                = 0
```

Full trace is in `trace.log`.

### What each syscall means

`openat()`
This asks the kernel to resolve the pathname, check permissions, and create an open-file entry for the process. The kernel returns a file descriptor, which is a small integer handle to that open file object. Modern libc uses `openat` instead of plain `open` for POSIX compliance.

`write()`
This copies bytes from user space into the kernel's control. For regular files, the kernel usually copies the data into the page cache and marks the page dirty. The actual device write can happen later.

`fsync()`
Forces the kernel to flush the file's dirty pages to the underlying storage device. This is the syscall that actually guarantees durability. Without it, a crash could lose the written data.

`read()`
This asks the kernel to copy bytes from the file into the program's memory. If the data is already cached from the previous write, the kernel returns it from RAM immediately.

`close()`
Releases the file descriptor. The close itself does not force a sync.

## Full Internal Flow

```
User Code (C++) -> POSIX syscall -> Kernel -> VFS -> File System -> Page Cache -> Disk
```

1. **User code**: the C++ program calls `open`/`write`/`read`.
2. **System call boundary**: at this point the process crosses from user space into kernel space. User programs cannot touch hardware directly; the kernel mediates everything.
3. **Kernel (VFS)**: the Virtual File System layer validates the request, resolves the file path through directory entries and inodes, checks permissions, and dispatches to the concrete file system.
4. **File system**: maps the logical filename to inode metadata and the file's data blocks. The inode stores metadata (size, permissions, timestamps) and block pointers; the directory entry stores the name-to-inode mapping.
5. **Page cache**: the kernel keeps file data pages in RAM. A write copies data here and marks the page dirty. A read checks here first; if present, no disk I/O is needed.
6. **Disk / DMA**: when the kernel decides to flush dirty pages (on `fsync`, memory pressure, or background writeback), the storage stack issues block I/O. DMA lets the storage controller transfer data directly between device and RAM without the CPU copying each byte.
7. **Back to user code**: the result is placed in the program's buffer and execution resumes.

## Core Concepts

### File Descriptor
A small integer returned by the kernel after `open`. The process uses it for subsequent `read`, `write`, `fsync`, and `close` calls. It is an index into the process file descriptor table.

### inode
Stores metadata about a file: ownership, permissions, timestamps, size, and pointers to the data blocks. The directory entry maps the human-readable filename to the inode number; the name lives in the directory, not the inode.

### System Calls
The controlled entry points from user space into the kernel. Only the kernel can access hardware, manage memory safely, and protect processes from each other.

### strace
Traces system calls made by a process. It shows real OS-level behavior instead of just the high-level code. For this lab it proves that file I/O eventually becomes kernel work.

### Page Cache
The kernel keeps frequently accessed file data in RAM to avoid disk I/O. A database buffer pool serves the same role at the application level but with finer control over eviction policy and consistency.

### fsync
Forces the kernel to write dirty pages for a file to stable storage. Databases call `fsync` (or equivalent) after writing WAL records to guarantee that committed transactions survive crashes.

### DMA
Direct Memory Access lets the storage controller transfer data between the device and RAM without the CPU copying every byte. The CPU sets up the transfer; the device and memory subsystem do the bulk of the work.

## Database Connection

Databases need disk because they must keep data after the process exits or the machine powers off. RAM alone cannot provide persistence.

Databases use RAM because access is dramatically faster than disk. This is why database systems keep hot pages in memory and try to avoid disk reads during query execution.

The OS page cache and a database buffer pool are the same idea: keep frequently used data in RAM and fall back to disk only when needed. Databases build their own buffer manager to control exactly which pages stay hot and when dirty pages are flushed.

This simple file program is a miniature version of the same storage path a database depends on: write to the OS, the OS caches in RAM, durability requires an explicit sync, reads come from cache when possible.

## Architecture Diagram

```
+------------------+
|   C++ Program    |  open() / write() / fsync() / read()
+--------+---------+
         |  system call boundary (user -> kernel)
+--------v---------+
|  Kernel (VFS)    |  path resolution, permission check, inode lookup
+--------+---------+
         |
+--------v---------+
|   File System    |  inode -> block pointers -> data blocks
+--------+---------+
         |
+--------v---------+
|   Page Cache     |  RAM buffer of file pages; writes go here first
+--------+---------+
         |  flush on fsync / memory pressure / background writeback
+--------v---------+
|  Storage Driver  |  translates block I/O to device commands
+--------+---------+
         |  DMA transfer
+--------v---------+
|      Disk        |  persistent storage
+------------------+
```

## Sample Output

```text
File written successfully: assignment-data.txt

Read back from file:

Advanced DBMS Lab 1
This file was written using POSIX open/write and read using open/read.
It demonstrates how user code reaches the operating system through system calls.
```

## Learnings

This lab made the file path concrete. A program does not magically read or write storage. It asks the kernel through system calls, which manages metadata, the page cache, and hardware. The same path is what makes database persistence possible: the WAL write, the buffer pool flush, the checkpoint -- all of it bottoms out in the same kernel I/O machinery traced here.

## Submission Contents

* `file_io.cpp`
* `README.md`
* `trace.log`
