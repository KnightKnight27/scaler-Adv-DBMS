# Lab 1: File I/O Tracing using strace (C++)

## Objective

The objective of this lab is to understand how file I/O operations in C++ interact with the Linux kernel using system calls and how data flows through inode, VFS, and page cache layers.

---

## Program Description

A simple C++ program was written to:

- Create and write to a file (`test.txt`)
- Read data from the file
- Display content on the console

This helps simulate basic file I/O operations.

---

## Execution Method

### Compile program
```bash
g++ file_io.cpp -o file_io
Run with strace
strace -o trace.log ./file_io
Observations from strace

Key system calls observed:

File Creation
openat() used to create/open file
File descriptor returned by kernel
Writing Data
write() system call writes data to file
Data first goes to page cache
Reading Data
read() system call retrieves data
May be served from page cache
File Close
close() releases file descriptor
System Architecture Flow
C++ Program
    ↓
libstdc++ (fstream)
    ↓
System Calls (openat, read, write, close)
    ↓
VFS (Virtual File System)
    ↓
inode layer
    ↓
Page Cache
    ↓
Disk (if required)
Key Learnings
Every file operation in C++ translates to Linux system calls
VFS provides abstraction over different file systems
inode stores file metadata and structure
Page cache improves performance by reducing disk I/O
strace is a powerful tool to trace system-level behavior

Conclusion

This lab demonstrates the complete journey of file I/O operations from a C++ program down to Linux kernel internals, showing how user-space operations interact with VFS, inode structures, and page cache before reaching physical storage