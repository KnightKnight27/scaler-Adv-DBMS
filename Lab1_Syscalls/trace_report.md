# Architectural Report: C++ File I/O & OS System Call Tracing

This document traces the path of a write operation from a standard C++ application down to the physical disk blocks, analyzing how Virtual File System (VFS), Page Cache, and physical Inodes interact.

## 1. The Syscall Trace Lifecycle

When executing `file_io.cpp`, the program interacts with the OS kernel through specific system calls. Here is the step-by-step trace:

### A. File Open (`openat`)
The application calls `std::ofstream::open()`, which translates to the `openat` system call:
```
openat(AT_FDCWD, "io_test.dat", O_WRONLY|O_CREAT|O_TRUNC, 0666) = 3
```
- **VFS Resolution**: The Virtual File System (VFS) resolves the path name. It queries the directory entry cache (dentry) and gets the Inode number of the parent directory.
- **Inode Allocation**: A new file is created, allocating a free physical Inode in the filesystem, updating the parent directory's block to map `"io_test.dat"` to this Inode.
- **FD Allocation**: The kernel allocates a file descriptor (typically `3`) pointing to the open file description in the system-wide table.

### B. Buffered Writing (`write`)
When `outfile.write()` is called and subsequently `.flush()` is triggered, the C++ standard library buffer is emptied, triggering the standard write syscall:
```
write(3, "\0\0\0\0a1b2c3d4-e5f6-7a8b-9c0d-1e2...", 4096) = 4096
```
- **Page Cache Interaction**: The kernel checks if the requested file blocks are present in the Page Cache. If they are, it writes the data directly to memory and marks the pages as **dirty**.
- **Asynchronous Nature**: The `write` system call returns immediately once the data is written to the Page Cache. The physical I/O does not happen synchronously unless requested.

### C. Forcing Durability (`fdatasync` / `fsync`)
When we want to guarantee that our data resides on the physical storage, we call a sync command (which in C++ can be mapped to calling `fsync` or `fdatasync` on POSIX systems):
```
fdatasync(3) = 0
```
- **fdatasync vs fsync**: `fsync` flushes both data blocks and filesystem metadata (modification time, access time, etc.). `fdatasync` only flushes the data blocks and the minimum metadata necessary to read the file (like the file size if it changed), reducing write amplification.

### D. File Resource Cleanup (`close`)
Upon stream destruction, `close` is triggered:
```
close(3) = 0
```
- **Reference Count**: The kernel decrements the reference count of the file description. When it reaches 0, the file descriptor and associated kernel buffers are released.

---

## 2. Kernel and Physical Data Path

Below is the horizontal visualization of a write operation traversing the OS layers:

```
+--------------------------------------------------------+
|                 C++ Application Stream                 |
+--------------------------------------------------------+
                            | (outfile.write() + flush())
                            v
+--------------------------------------------------------+
|        VFS (Virtual File System Translation Layer)     |
+--------------------------------------------------------+
                            | (Translates path to Inode)
                            v
+--------------------------------------------------------+
|       OS Page Cache (Dirty Page Allocation / Buffers)  |
+--------------------------------------------------------+
                            | (fdatasync() syscall)
                            v
+--------------------------------------------------------+
|      Block Device Layer (I/O Scheduler & Queues)       |
+--------------------------------------------------------+
                            | (Device Driver Commands)
                            v
+--------------------------------------------------------+
|         Physical Storage (HDD/SSD Flash Blocks)        |
+--------------------------------------------------------+
```

### File System Inodes and Page Cache Alignment
- **Inode**: The Inode contains metadata about the file (permissions, size, timestamps) and pointers to direct/indirect data blocks on the disk.
- **Page Size Alignment**: OS pages are typically 4KB. Writing in multiples of 4KB aligns perfectly with both the OS page size and the physical SSD erase block sizes (which are multiples of 4KB). Writing misaligned blocks leads to "Read-Modify-Write" cycles, degrading performance and increasing wear.
