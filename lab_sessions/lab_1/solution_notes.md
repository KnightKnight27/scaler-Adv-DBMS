# Lab 1 — File I/O in C++: Kernel Journey via strace

## Concept

Every time a C++ program opens a file with `std::ifstream`, it doesn't go directly to the disk. It crosses multiple layers: the C++ standard library → libc → a syscall boundary → the kernel's VFS → the filesystem driver → the page cache → (only on a cold miss) the physical disk.

The goal of this lab is to make that invisible journey visible using `strace` (Linux) or `dtruss` (macOS).

## Approach

1. Write the simplest possible C++ file reader using `std::ifstream` and `std::getline`.
2. Compile and run it to confirm it works.
3. Trace the syscalls it makes — specifically `openat`, `fstat`, `read`, and `close`.
4. Inspect the inode metadata of the file using `stat` and `ls -i`.
5. Map what we see in the trace back to each kernel layer.

## Solution

`reader.cpp` opens `test.txt` with `std::ifstream`, reads line by line, and prints to stdout.

### Key syscall sequence (Linux strace output):
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3   ← kernel resolves path, finds inode, returns fd
fstat(3, {st_size=17, ...})            = 0   ← reads inode metadata (no disk I/O if cached)
read(3, "hello from lab 1\n", 4096)    = 17  ← copies 17 bytes from page cache to userspace
read(3, "", 4096)                      = 0   ← returns 0 = EOF, std::getline exits loop
close(3)                               = 0   ← releases fd, decrements inode refcount
```

### What each layer does:
```
std::ifstream::getline()
    └── libc buffered I/O
        └── read() syscall          ← user / kernel boundary
            └── VFS (Virtual Filesystem Switch)
                └── ext4 / APFS driver
                    └── Page cache  ← warm: served from RAM, no disk hit
                        └── Block device → NVMe (cold miss only)
```

### Inode check:
```bash
ls -i test.txt      # inode number: 36441654
stat test.txt       # size: 17 bytes, blocks: 8 (one 4K block)
```

## Key Takeaway

The file descriptor is a per-process integer. The inode is the kernel's canonical object for the file. `strace` exposes exactly where user-space ends and the kernel begins. The page cache means re-reading the same file costs only a memory access, not a disk seek.

> macOS note: use `sudo dtruss ./reader` instead of strace, or `sudo fs_usage -f filesys ./reader`.
