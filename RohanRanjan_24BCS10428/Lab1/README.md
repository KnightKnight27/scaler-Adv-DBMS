# Lab 1 — File I/O in C++: the Kernel Journey via `strace`

**Rohan Ranjan — 24BCS10428**

## Objective
Understand what happens under the hood when C++ opens and reads a file: from the
syscall layer down to inodes, the VFS, and kernel interactions.

## Files
| File            | Purpose                                                        |
|-----------------|----------------------------------------------------------------|
| `reader.cpp`    | Minimal `std::ifstream` reader that prints a file line by line |
| `run_strace.sh` | Builds the reader, creates `test.txt`, and runs `strace`       |

## Build & run
```bash
echo "hello from lab 1" > test.txt
g++ -std=c++17 -o reader reader.cpp
strace -e trace=openat,read,close,fstat,mmap ./reader
```
Or simply: `bash run_strace.sh`

## Syscalls observed
| Syscall   | What it does                                                       |
|-----------|--------------------------------------------------------------------|
| `openat`  | Opens the file; returns a file descriptor (fd)                     |
| `fstat`   | Fetches inode metadata (size, permissions, timestamps) for the fd  |
| `read`    | Reads bytes from the file into a user-space buffer                 |
| `mmap`    | Maps the C++ runtime / shared libs into the process address space  |
| `close`   | Releases the fd, decrements the inode reference count              |

Condensed trace:
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0    # EOF
close(3)                                = 0
```

## The inode journey
When `openat` is called, the kernel:
1. **Path resolution** — walks the directory tree from `AT_FDCWD` component by component.
2. **Inode lookup** — each directory entry maps a filename → inode number; the kernel
   fetches the inode from the filesystem (ext4/btrfs/…) via the VFS layer.
3. **Permission check** — compares the process UID/GID against the inode's `st_uid`,
   `st_gid`, `st_mode`.
4. **File descriptor allocation** — the kernel allocates an fd in the process's open-file
   table, pointing to a `struct file` that holds the current offset and a pointer to the inode.

Inspect the inode directly:
```bash
ls -i test.txt        # e.g. 1234567 test.txt
stat test.txt         # Inode: 1234567, Links: 1, Size: 18, Blocks: 8
```

## Kernel layers involved
```
C++ std::ifstream
      |
   fread / libc
      |
   read() syscall      <-- user/kernel boundary
      |
   VFS (Virtual Filesystem Switch)
      |
   Filesystem driver (ext4 / btrfs)
      |
   Page cache (served from RAM if recently accessed)
      |
   Block device driver -> physical disk (on cache miss)
```
- The **page cache** means repeated reads of the same file don't hit disk.
- `fstat` is cheap: inode metadata is cached in the kernel's inode cache (icache).

## Verify with /proc
```bash
ls -l /proc/<PID>/fd          # open fds while the program runs
stat   /proc/<PID>/fd/3       # the inode backing fd 3
```

## Key takeaways
- Every `std::ifstream` open ultimately calls `openat`, which traverses the VFS and resolves an inode.
- The file descriptor is a per-process handle; the inode is the kernel's canonical file representation.
- `strace` exposes the exact syscall boundary between user-space C++ and the kernel.
- The page cache absorbs repeated reads; only cold reads go to disk.
