# Lab Session 1: File I/O in C++ — Kernel Journey via strace

## Objective
Understand what happens under the hood when C++ opens and reads a file: from the syscall layer down to inodes, the VFS, and kernel interactions.

---

## Code Structure

- [reader.cpp](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%201/reader.cpp): The standard C++ file reader.
- [Makefile](file:///Users/kushalsacharya/Desktop/scaler-Adv-DBMS/lab%201/Makefile): Defines compile, run, and clean rules.

---

## Build and Run Instructions

To compile the application:
```bash
make
```

To run the reader (which automatically generates a `test.txt` with contents `"hello from lab 1"` and reads it):
```bash
make run
```

To clean up binary files and generated text files:
```bash
make clean
```

---

## The Journey of a File Operation

### 1. Tracing with `strace` (on Linux)
To trace system calls, run the compiled `reader` executable with `strace`:
```bash
strace -e trace=openat,read,close,fstat,mmap ./reader
```

Key syscalls observed:

| Syscall    | What it does                                                          |
|------------|-----------------------------------------------------------------------|
| `openat`   | Opens the file; returns a file descriptor (fd)                        |
| `fstat`    | Fetches inode metadata (size, permissions, timestamps) for the fd     |
| `read`     | Reads bytes from the file into a user-space buffer                    |
| `mmap`     | Maps the C++ runtime / shared libs into process address space         |
| `close`    | Releases the fd, decrements inode reference count                     |

#### Trace Example (Condensed)
```
openat(AT_FDCWD, "test.txt", O_RDONLY) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=18, ...}) = 0
read(3, "hello from lab 1\n", 4096)    = 18
read(3, "", 4096)                       = 0    # EOF
close(3)                                = 0
```

---

### 2. The Inode Journey

When `openat` is called, the kernel executes the following steps:
1. **Path resolution**: Walks the directory tree from `AT_FDCWD` component by component.
2. **Inode lookup**: Each directory entry maps a filename → inode number. The kernel fetches the inode from the underlying filesystem (ext4/btrfs/etc.) via the VFS layer.
3. **Permission check**: Compares the process's UID/GID against the inode's `st_uid`, `st_gid`, and `st_mode`.
4. **File descriptor allocation**: The kernel allocates a file descriptor (fd) in the process's open-file table, pointing to a `struct file` containing the current offset and a pointer to the inode.

To check the inode number of a file:
```bash
ls -i test.txt
# e.g.: 1234567 test.txt

stat test.txt
# shows: Inode: 1234567, Links: 1, Size: 18, Blocks: 8
```

---

### 3. Kernel Layers Involved

```
C++ std::ifstream
      |
   fread / libc
      |
   read() syscall  <-- user/kernel boundary
      |
   VFS (Virtual Filesystem Switch)
      |
   Filesystem driver (ext4 / btrfs)
      |
   Page cache (if file was recently accessed, no disk I/O)
      |
   Block device driver → physical disk (on cache miss)
```

- **Page Cache**: Repeated reads of the same file are served directly from RAM without hitting physical disks.
- **Inode Cache (icache)**: `fstat` queries are cheap since inode metadata is cached inside kernel memory.

---

### 4. Verification with `/proc` (on Linux)

While the program is running (you can add a sleep or checkpoint), you can inspect its open file descriptors:
```bash
ls -l /proc/<PID>/fd

# See the inode backing fd 3:
stat /proc/<PID>/fd/3
```

---

## Key Takeaways
- Every `std::ifstream` open ultimately triggers `openat`, which traverses the VFS and resolves an inode.
- The file descriptor is a per-process handle, whereas the inode is the kernel's canonical representation of the file.
- `strace` exposes the exact syscall boundary between user-space C++ and the kernel.
- The page cache optimizes sequential/repeated reads; only cold reads trigger actual block device reads.
