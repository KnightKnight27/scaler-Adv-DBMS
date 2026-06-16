# Lab Session 1: File I/O in C++ — Kernel Journey via strace

## Objective

Understand how a simple C++ file read operation interacts with the Linux kernel, the Virtual File System (VFS), inodes, and the page cache.

## Steps Performed

### 1. Implemented a File Reader

A simple C++ program using `std::ifstream` was created to open and read a text file line by line.

### 2. Traced System Calls

The program was executed under `strace` using:

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

Observed system calls:

* `openat`
* `fstat`
* `read`
* `mmap`
* `close`

### 3. Studied the Inode Journey

When a file is opened:

1. Path resolution occurs.
2. The filesystem resolves the inode.
3. Permissions are checked.
4. A file descriptor is allocated.

### 4. Kernel Layers Involved

```text
C++ std::ifstream
      |
   libc
      |
   read() syscall
      |
      VFS
      |
 Filesystem Driver
      |
   Page Cache
      |
 Block Device Driver
```

### 5. Verification Using /proc

Open file descriptors were inspected using:

```bash
ls -l /proc/<PID>/fd
```

and inode mappings were checked with:

```bash
stat /proc/<PID>/fd/3
```

## Key Learnings

* `std::ifstream` eventually invokes kernel syscalls.
* File descriptors are process-specific handles.
* Inodes are the kernel's representation of files.
* The page cache improves read performance.
* `strace` provides visibility into user-space to kernel interactions.
