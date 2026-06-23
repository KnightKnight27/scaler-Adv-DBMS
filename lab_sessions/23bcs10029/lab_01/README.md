# Lab 1: File I/O in C++ — Kernel Journey via strace

## What this lab covers

Tracing the syscall path from `std::ifstream` through the kernel VFS layer to the inode, using `strace`.

## Build & Run

```bash
echo "hello from lab 1" > test.txt
g++ -std=c++17 -o file_io file_io.cpp
./file_io

# Trace syscalls
strace -e trace=openat,read,close,fstat,mmap ./file_io
```

## Key syscalls observed

| Syscall  | What it does                                                      |
|----------|-------------------------------------------------------------------|
| `openat` | Opens file; returns a file descriptor (fd)                        |
| `fstat`  | Fetches inode metadata (size, permissions, timestamps)            |
| `read`   | Reads bytes from file into user-space buffer                      |
| `mmap`   | Maps C++ runtime / shared libs into process address space         |
| `close`  | Releases the fd, decrements inode reference count                 |

## Condensed strace output

```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0    # EOF
close(3)                                = 0
```

## Kernel layers

```
std::ifstream
    |
 fread / libc
    |
 read() syscall   <-- user/kernel boundary
    |
 VFS (Virtual Filesystem Switch)
    |
 Filesystem driver (ext4 / btrfs)
    |
 Page cache  (cache hit = no disk I/O)
    |
 Block device driver -> physical disk
```

## Key takeaways

- Every `std::ifstream` open calls `openat`, which traverses the VFS and resolves an inode.
- The file descriptor is a per-process handle; the inode is the kernel's canonical representation.
- The page cache absorbs repeated reads — only cold reads hit disk.
- `strace` exposes the exact syscall boundary between user-space C++ and the kernel.
