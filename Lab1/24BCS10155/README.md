# Lab Session 1: File I/O in C++ — Kernel Journey via strace

**Name:** Snehangshu Roy
**Roll No:** 24BCS10155

## Objective
Understand what happens under the hood when C++ opens and reads a file: from
the syscall layer down to inodes, the VFS, and kernel interactions.

## Files
- `main.cpp` — minimal C++ file reader using `std::ifstream`.
- `test.txt` — sample input file.
- `makefile` — build / run / trace targets.

## Build & Run
```bash
make            # builds ./reader
make run        # runs ./reader test.txt
```

## Trace with strace (Linux)
```bash
make trace
# or
strace -e trace=openat,read,close,fstat,mmap ./reader
```

Key syscalls observed:

| Syscall  | What it does                                                        |
|----------|---------------------------------------------------------------------|
| `openat` | Opens the file; returns a file descriptor (fd)                      |
| `fstat`  | Fetches inode metadata (size, permissions, timestamps) for the fd   |
| `read`   | Reads bytes from the file into a user-space buffer                  |
| `mmap`   | Maps the C++ runtime / shared libs into process address space       |
| `close`  | Releases the fd, decrements inode reference count                   |

Condensed trace:
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=17, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 17
read(3, "", 4096)                       = 0    # EOF
close(3)                                = 0
```

## The inode journey
When `openat` is called, the kernel:
1. **Path resolution** — walks the directory tree from `AT_FDCWD` component by component.
2. **Inode lookup** — each directory entry maps a filename → inode number; the
   kernel fetches the inode via the VFS layer.
3. **Permission check** — compares process UID/GID against the inode's `st_uid`,
   `st_gid`, `st_mode`.
4. **File descriptor allocation** — kernel allocates an fd in the process's
   open-file table pointing to a `struct file` (current offset + inode pointer).

Inspect the backing inode:
```bash
ls -i test.txt          # filename -> inode number
stat test.txt           # Inode, Links, Size, Blocks
```

## Kernel layers involved
```
C++ std::ifstream
  -> fread / libc
  -> read() syscall          <-- user/kernel boundary
  -> VFS (Virtual Filesystem Switch)
  -> Filesystem driver (ext4 / btrfs)
  -> Page cache (served from RAM on a warm read)
  -> Block device driver -> physical disk (only on a cache miss)
```

## Verify via /proc (add a sleep in the program first)
```bash
ls -l /proc/<PID>/fd          # open fds
stat /proc/<PID>/fd/3         # the inode backing fd 3
```

## Key Takeaways
- Every `std::ifstream` open ultimately calls `openat`, which traverses the VFS
  and resolves an inode.
- The file descriptor is a per-process handle; the inode is the kernel's
  canonical representation of the file.
- `strace` exposes the exact syscall boundary between user-space C++ and the kernel.
- The page cache absorbs repeated reads; only cold reads go to disk.
