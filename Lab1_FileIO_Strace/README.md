# Lab 1 - File I/O Tracing with Raw System Calls

**Name:** Abhijit P
**Roll No:** 24BCS10175

## Objective

The goal of this lab is to understand how file operations are performed inside Linux using raw system calls instead of high-level C++ file handling libraries.

The programs directly invoke Linux kernel system calls to:

* Open a file
* Read data from a file
* Write data to a file
* Close a file

This helps visualize the complete path taken by data from the application layer to storage and back.

---

## Files Included

### write.cpp

Creates or opens a file named `test.txt` and writes text into it using raw Linux system calls.

### read.cpp

Opens `test.txt`, reads its contents, and prints the data to standard output using raw Linux system calls.

### test.txt

Sample file used during execution.

---

## System Calls Used

The programs invoke Linux kernel services directly through the `syscall()` interface.

### openat()

Used to open or create a file.

```cpp
syscall(SYS_openat, AT_FDCWD, filename, O_CREAT | O_WRONLY, 0644);
```

### read()

Reads data from a file descriptor into memory.

```cpp
syscall(SYS_read, fd, buffer, sizeof(buffer));
```

### write()

Writes data either to a file or to standard output.

```cpp
syscall(SYS_write, fd, data, strlen(data));
```

### close()

Releases the file descriptor.

```cpp
syscall(SYS_close, fd);
```

---

## Program Execution Flow

### Write Program

```text
openat()
   ↓
write()
   ↓
close()
```

The file is opened, data is written into it, and the file descriptor is released.

### Read Program

```text
openat()
   ↓
read()
   ↓
write(stdout)
   ↓
close()
```

The file is opened, data is read into memory, displayed on the terminal, and then the file is closed.

---

## File Descriptor Concept

Linux does not use filenames internally after a file is opened.

Instead, the kernel returns a file descriptor.

Example:

```text
openat(...) = 3
```

Common descriptors:

```text
0 → stdin
1 → stdout
2 → stderr
3 → first opened file
```

All subsequent operations use the file descriptor rather than the file name.

---

## strace Analysis

The programs were traced using:

```bash
strace ./write
strace ./read
```

### Write Program

```text
openat(...) = 3
write(3, "Hello from raw syscall!", 24) = 24
close(3) = 0
```

### Read Program

```text
openat(...) = 3
read(3, "Hello from raw syscall!", 100) = 24
write(1, "Hello from raw syscall!", 24) = 24
close(3) = 0
```

These traces show the exact interactions between the application and the Linux kernel.

---

## Inode and File Storage

Every file in Linux is represented internally by an inode.

```text
Filename
    ↓
Inode
    ↓
Data Blocks
```

The inode stores:

* File size
* Permissions
* Ownership information
* Timestamps
* Locations of disk blocks

The actual file contents are stored separately in data blocks.

---

## VFS (Virtual File System)

Linux uses a Virtual File System (VFS) layer to provide a common interface to different file systems.

```text
Application
    ↓
System Call
    ↓
VFS
    ↓
File System (ext4, xfs, btrfs, ...)
```

The application does not need to know which file system is being used.

---

## Page Cache

Linux uses a page cache to improve file I/O performance.

### Write Path

```text
Application
    ↓
write()
    ↓
Kernel Page Cache
    ↓
Disk (later flush)
```

Data is typically written to memory first and later flushed to storage.

### Read Path

If data is already cached:

```text
Page Cache
    ↓
Application
```

If data is not cached:

```text
Disk
    ↓
Page Cache
    ↓
Application
```

This reduces disk access and improves performance.

---

## Complete Storage Journey

### Write Operation

```text
User Program
    ↓
SYS_write
    ↓
Kernel
    ↓
VFS
    ↓
Inode Lookup
    ↓
Page Cache
    ↓
Disk Blocks
```

### Read Operation

```text
Disk Blocks
    ↓
Page Cache
    ↓
Kernel
    ↓
SYS_read
    ↓
User Buffer
    ↓
Terminal Output
```

---

## Relation to DBMS

Database systems rely heavily on the same storage concepts.

### Pages

DBMS data is organized into fixed-size pages.

### Buffer Pool

Pages are cached in memory before accessing disk repeatedly.

### Eviction

When memory becomes full, pages are removed according to a replacement policy.

### Disk Blocks

Database pages are ultimately stored on physical disk blocks managed by the operating system.

---

## Conclusion

This lab provided practical exposure to Linux file I/O at the system-call level. By directly invoking kernel services and analyzing them using `strace`, it became possible to understand the complete journey of data through system calls, VFS, inodes, page cache, and disk storage. These concepts form the foundation of how modern database systems manage persistent data efficiently.
