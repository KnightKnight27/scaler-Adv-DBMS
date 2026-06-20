# Lab 1: C++ File I/O — Kernel Journey with strace

## Objective

Understand how a C++ file read travels through Linux system calls, the VFS, an
inode, and the page cache.

## Files

```text
reader.cpp   C++ file reader
test.txt     Input file
README.md    Commands and observations
```

Run this lab on Linux, WSL, or a Linux virtual machine because `strace` and
`/proc` are Linux features.

## 1. Compile and run

```bash
g++ -std=c++17 -Wall -Wextra reader.cpp -o reader
./reader
```

Output:

```text
hello from lab 1
```

The program uses `std::ifstream`, but the C++ library eventually uses Linux
system calls to open and read the file.

## 2. Trace the system calls

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

A condensed trace looks like this. File descriptors and buffer sizes may vary:

```text
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=17, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 17
read(3, "", 4096)                      = 0
close(3)                                = 0
```

The complete trace also contains calls made while loading the C++ runtime and
shared libraries.

| System call | Purpose |
|---|---|
| `openat` | Opens `test.txt` and returns a file descriptor |
| `fstat` | Reads file metadata associated with the inode |
| `read` | Copies file bytes into the program's buffer |
| `mmap` | Maps shared libraries or file-backed pages into memory |
| `close` | Releases the file descriptor |

## 3. Inode journey

```bash
ls -i test.txt
stat test.txt
```

When `openat` runs:

1. The VFS resolves `test.txt` in the current directory.
2. Its directory entry points to an inode.
3. Linux checks the inode's ownership and permissions.
4. Linux creates an open-file object and returns a file descriptor.
5. `read` obtains the file data through the page cache.

The file descriptor is a handle used by one process. The inode is the
filesystem's representation of the file and stores metadata such as its size,
owner, permissions, and block locations.

## 4. Kernel layers

```text
C++ std::ifstream
        |
        v
C++ runtime / libc
        |
        v
read() system call  <-- user/kernel boundary
        |
        v
VFS -> filesystem driver -> page cache
                              |
                              v (cache miss)
                    block device -> physical disk
```

If the file page is already in the page cache, Linux serves it from RAM. A disk
read is needed only on a cache miss. Inode metadata can similarly be served from
the kernel's inode cache.

## 5. Verify with `/proc`

The optional `--pause` argument keeps the file open for 30 seconds:

```bash
./reader --pause &
PID=$!
ls -l /proc/$PID/fd
readlink /proc/$PID/fd/*
```

Find the descriptor pointing to `test.txt`, then inspect it. It is commonly `3`,
but the number can vary:

```bash
stat /proc/$PID/fd/3
stat test.txt
wait $PID
```

Both `stat` commands should report the same inode because the descriptor points
to the same file.

## Key takeaways

- `std::ifstream` ultimately crosses into the kernel through system calls.
- The VFS resolves a path to an inode and returns a file descriptor.
- `strace` shows the user-to-kernel system-call boundary.
- The page cache allows repeated reads to be served from RAM.
