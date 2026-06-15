# Lab 1: C++ File I/O and the Kernel Journey

**Name:** Aditya Bhaskara  **Roll No:** 24BCS10058  **Lab Session:** 1

## Objective

Follow what actually happens when a C++ program opens and reads a file: from the
`std::ifstream` call, down through libc to the `read()` syscall, across the
user/kernel boundary into the VFS, the inode, the page cache and the filesystem.

This was run on macOS (Apple clang). The Linux toolchain (`g++`, `strace`,
`/proc`) is noted alongside the macOS equivalent (`clang++`, `fs_usage`, `lsof`) at
each step.

---

## 1. The program

`reader.cpp` opens a file and prints it line by line. The C++ is intentionally
plain so the interesting part is the syscalls it generates, not the code.

```cpp
std::ifstream file(path);          // becomes open()/openat()
std::string line;
while (std::getline(file, line))   // first getline triggers read()
    std::cout << line_no++ << ": " << line << "\n";
// stream closes at end of scope    becomes close()
```

Build and run:

```
$ g++ -std=c++17 -o reader reader.cpp
$ printf 'hello from lab 1\nthis file is read line by line\ntracing reveals the syscalls\n' > test.txt
$ ./reader test.txt
1: hello from lab 1
2: this file is read line by line
3: tracing reveals the syscalls
```

---

## 2. The inode behind the file

A filename is just a directory entry pointing at an inode, and the inode is the
kernel's real record of the file: its size, permissions, timestamps and the
blocks that hold its data. `stat` reads that inode:

```
$ stat -f "inode=%i  size=%z bytes  blocks=%b  perms=%Sp  links=%l" test.txt
inode=192718210  size=77 bytes  blocks=8  perms=-rw-r--r--  links=1
```

What each field tells us:

| Field    | Value         | Meaning                                                    |
|----------|---------------|------------------------------------------------------------|
| `inode`  | 192718210     | The inode number, the file's identity within its filesystem |
| `size`   | 77 bytes      | Logical length, the 77 bytes we wrote                       |
| `blocks` | 8             | 512 byte blocks reserved on disk (8 * 512 = 4096, one page) |
| `perms`  | -rw-r--r--    | Regular file, owner read/write, group and other read       |
| `links`  | 1             | One name points at this inode                              |

The 77 byte file occupies a full 4096 byte page worth of blocks, which is the
same page granularity that showed up in the SQLite file in Lab 2. The filesystem
allocates space in blocks, not bytes.

On Linux `ls -i test.txt` prints the same inode number, and `stat test.txt` shows
`Inode:` and `Blocks:` directly.

---

## 3. Tracing the syscalls

The syscall boundary is where the C++ stops and the kernel begins. On Linux:

```
$ strace -e trace=openat,read,close,fstat,mmap ./reader test.txt
```

On macOS the closest equivalent is `dtruss`, but it is driven by DTrace, whose
syscall provider is disabled by System Integrity Protection. Running it on this
machine confirms exactly that:

```
$ sudo dtruss ./reader test.txt
dtrace: system integrity protection is on, some features will not be available
dtrace: failed to initialize dtrace: DTrace requires additional privileges
```

The SIP-safe tool on macOS is `fs_usage`, which reads filesystem events from the
kernel's kdebug facility. Tracing the `reader` process captures its real syscalls
(paths shortened for readability; note how descriptors are reused, so the same
low fd numbers reappear):

```
open      F=3  (R)  .                                   # resolve the current directory
openat    F=4  (R)  [3]/../../System/.../Cryptexes/OS   # openat relative to a directory fd
fstatat64           [4]/System/Library/dyld             # stat an inode, relative to fd 4
stat64              ~/lab1/reader                        # fetch the binary's inode metadata
open      F=3  (R)  ~/lab1/reader                        # open it
mmap      F=0  A=0x...                                   # map it into the address space
close     F=3                                            # release the descriptor
```

Those are the same primitives the program uses for `test.txt`: `openat` walks the
path to an inode, `stat`/`fstat` fetches that inode's metadata, `mmap` maps pages,
and `close` releases the descriptor. On Linux, `strace` shows the read of
`test.txt` itself most directly, which is the cleanest view of the journey:

```
openat(AT_FDCWD, "test.txt", O_RDONLY)   = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=77, ...}) = 0
read(3, "hello from lab 1\nthis file is re"..., 8191) = 77
read(3, "", 8191)                        = 0      # 0 bytes means end of file
close(3)                                 = 0
```

What each syscall does:

| Syscall  | Role                                                                 |
|----------|----------------------------------------------------------------------|
| `openat` | Resolves the path to an inode, checks permissions, returns fd 3      |
| `fstat`  | Reads the inode metadata (size, mode, timestamps) for that fd        |
| `read`   | Copies bytes from the file into a user buffer; returns the byte count |
| `mmap`   | Maps the C++ runtime and shared libraries into the address space     |
| `close`  | Releases fd 3 and drops the inode reference count                    |

fd 3 is the first free descriptor because 0, 1 and 2 are already taken by stdin,
stdout and stderr.

---

## 4. What openat does inside the kernel

1. **Path resolution.** Starting from the current directory (`AT_FDCWD`), the
   kernel walks the path one component at a time.
2. **Inode lookup.** Each directory entry maps a name to an inode number; the VFS
   asks the filesystem driver for that inode, or finds it already in the inode
   cache.
3. **Permission check.** The process UID/GID is checked against the inode's owner,
   group and mode bits.
4. **Descriptor allocation.** The kernel allocates a file descriptor in the
   process table pointing at a `struct file`, which holds the current read offset
   and a pointer to the inode.

---

## 5. The layers a read crosses

```
std::ifstream / std::getline
        |
   libc buffering (fread)
        |
   read() syscall            <- user / kernel boundary
        |
   VFS (virtual filesystem switch)
        |
   filesystem driver (APFS on macOS, ext4 on Linux)
        |
   page cache                <- served from RAM if the page is already cached
        |
   block device  ->  physical disk   (only on a cache miss)
```

The page cache is why reading the same file twice is much faster the second time:
the first read pulls pages from disk into RAM, and every later read of those same
pages is served from memory with no disk I/O. `fstat` is similarly cheap because
inode metadata is kept in the kernel's inode cache.

---

## 6. Watching the open descriptor

While the process is alive, Linux exposes its open files under `/proc`:

```
$ ls -l /proc/<pid>/fd      # fd 3 -> /path/to/test.txt
$ stat /proc/<pid>/fd/3     # the inode backing fd 3
```

macOS has no `/proc`; the equivalent is `lsof`:

```
$ lsof -p <pid>             # lists every fd the process holds open
```

Because the reader finishes in milliseconds, observing this live needs a brief
`sleep` added to the program, after which both tools show fd 3 pointing back at
the same inode reported by `stat` in section 2.

---

## Key takeaways

- Every `std::ifstream` open ends up as an `openat`/`open` syscall that walks the
  VFS and resolves an inode.
- The file descriptor is a per-process handle; the inode is the kernel's single
  canonical record of the file, shared by every descriptor that opens it.
- `strace` on Linux and `fs_usage` on macOS expose the exact user/kernel boundary
  (`dtruss` would too, but its DTrace backend is blocked by SIP);
  the key markers are the open, the read that returns the byte count, the read
  that returns 0 at end of file, and the close.
- Files are allocated in blocks and cached in pages, so a 77 byte file still
  reserves a full page, and repeated reads come from the page cache rather than
  disk.
