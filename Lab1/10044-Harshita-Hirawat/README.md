# Lab 1: File I/O Using Raw Linux System Calls

## Aim

The aim of this lab is to understand how file operations work at a low level by performing file creation, writing, reading, and closing using raw Linux system calls from C++.

Instead of using high-level APIs like `fstream`, `printf`, or libc wrappers such as `open()`, `read()`, `write()`, and `close()`, the program directly invokes the Linux `syscall` instruction through inline assembly.

## Files

```text
file_io_syscalls.cpp   C++ implementation
README.md              Lab explanation
example.txt            Generated when the program runs
```

## Platform Requirement

This program is meant for:

```text
Linux x86-64
```

It uses Linux syscall numbers and GCC-style inline assembly, so it should be run on Linux, WSL, or a Linux virtual machine.

## Build and Run

Compile:

```bash
g++ -std=c++17 -Wall -Wextra -O2 file_io_syscalls.cpp -o file_io_syscalls
```

Run:

```bash
./file_io_syscalls
```

Expected output is similar to:

```text
File descriptor used for reading: 3

Data read from file:
Hello from C++ using raw Linux system calls.
This file was opened, written, closed, opened again, and read back.

Completed open/write/read/close using raw syscalls.
```

## System Calls Used

| System call | Purpose |
|---|---|
| `SYS_openat` | Opens or creates `example.txt` |
| `SYS_write` | Writes data to a file or terminal |
| `SYS_read` | Reads data from the file |
| `SYS_close` | Closes the file descriptor |
| `SYS_exit` | Exits immediately on serious error |

The program uses `openat` with `AT_FDCWD`, which means the file path is resolved relative to the current working directory.

## How Raw Syscalls Are Made

The helper function `raw_syscall6` places the syscall number and arguments into the correct Linux x86-64 registers and then executes:

```asm
syscall
```

Register mapping:

| Register | Use |
|---|---|
| `rax` | System call number and return value |
| `rdi` | Argument 1 |
| `rsi` | Argument 2 |
| `rdx` | Argument 3 |
| `r10` | Argument 4 |
| `r8` | Argument 5 |
| `r9` | Argument 6 |

This bypasses normal library functions and talks directly to the kernel.

## Program Flow

1. Create or truncate `example.txt` using `SYS_openat`.
2. Write text into the file using `SYS_write`.
3. Close the write file descriptor using `SYS_close`.
4. Reopen the same file in read-only mode.
5. Read the file in 64-byte chunks using `SYS_read`.
6. Print the read data to standard output using `SYS_write`.
7. Close the read file descriptor.

The file is opened for writing with:

```cpp
O_CREAT | O_WRONLY | O_TRUNC
```

The permission used is:

```text
0644
```

This means the owner can read and write, while group and others can only read.

## File Descriptor Idea

A file descriptor is a small integer returned by the kernel for an open file.

Common descriptors are:

| Descriptor | Meaning |
|---:|---|
| `0` | Standard input |
| `1` | Standard output |
| `2` | Standard error |
| `3+` | Files opened by the process |

In this program, the opened file usually receives descriptor `3` because descriptors `0`, `1`, and `2` are already reserved.

## What Happens Inside the Kernel

When a syscall runs, the CPU switches from user mode to kernel mode. The kernel validates the syscall number, arguments, file permissions, and file descriptor.

For file access, the kernel uses structures such as:

- File descriptor table
- Open file object
- Directory entry
- Inode
- Page cache
- Filesystem and block layer
- Device driver

Reads may be served from the page cache if the data is already in memory. Writes may first update dirty pages in the page cache and later be flushed to disk.

## Relation to DBMS

This lab is important for DBMS because databases ultimately store tables, indexes, logs, and pages inside files.

A DBMS storage engine must understand:

- How data moves between user space and kernel space
- How file descriptors represent open files
- Why page cache affects performance
- Why disk I/O is slower than memory access
- How low-level reads and writes support database pages

So even though this program is small, it demonstrates the same basic file I/O path used underneath larger database systems.

## Conclusion

This lab showed how a C++ program can perform file I/O without high-level libraries by directly using Linux system calls. It helped connect simple file operations with kernel concepts such as syscalls, descriptors, inodes, page cache, and storage I/O, which are all important for understanding DBMS storage internals.
