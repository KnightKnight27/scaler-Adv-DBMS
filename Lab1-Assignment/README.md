# Lab Session 1 — Solution: File I/O in C++, the Kernel Journey via `strace`

This directory contains my completed solution to **`lab_sessions/lab_1.txt`**.

The goal of the lab is to understand what actually happens when a C++ program
opens and reads a file — from the `std::ifstream` call in user space, across the
syscall boundary, through the VFS and filesystem driver, down to inodes and the
page cache.

Everything below is **reproduced from real runs on this machine**
(Ubuntu 24.04, g++ 13.3.0, ext-family filesystem). Where the live output differs
from the idealized example in the lab text, I have called it out — those
differences are the most instructive part.

## Files in this directory

| File | Purpose |
|------|---------|
| `reader.cpp` | The simple `std::ifstream` file reader from the lab |
| `reader_sleep.cpp` | Same reader, but holds the file open for 3 s (so the fd can be inspected via `/proc` while the process is alive) |
| `test.txt` | Input data file (`hello from lab 1`) |
| `strace_filtered.txt` | Captured `strace` output (committed as evidence) |
| `README.md` | This write-up |

> Compiled binaries (`reader`, `reader_sleep`) are build artifacts and are not committed. Rebuild with the commands below.

---

## Step 1 — The C++ file reader

[`reader.cpp`](reader.cpp):

```cpp
#include <iostream>
#include <fstream>
#include <string>

int main() {
    std::ifstream file("test.txt");
    if (!file.is_open()) {
        std::cerr << "Failed to open file\n";
        return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
        std::cout << line << "\n";
    }
    return 0;
}
```

Build and create the input file:

```bash
echo "hello from lab 1" > test.txt
g++ -o reader reader.cpp
./reader
# -> hello from lab 1
```

---

## Step 2 — Trace with `strace`

```bash
strace -e trace=openat,read,close,fstat,mmap ./reader 2> strace_filtered.txt
```

The trace splits cleanly into **two phases**.

### Phase A — Dynamic loader brings the program to life (before `main`)

Long before our `test.txt` is touched, the dynamic linker maps the C++ runtime
into the address space. This is the bulk of the trace:

```
openat(AT_FDCWD, "/etc/ld.so.cache", O_RDONLY|O_CLOEXEC) = 3
fstat(3, {st_mode=S_IFREG|0644, st_size=88747, ...}) = 0
mmap(NULL, 88747, PROT_READ, MAP_PRIVATE, 3, 0) = 0x...
close(3)
openat(AT_FDCWD, "/lib/x86_64-linux-gnu/libstdc++.so.6", O_RDONLY|O_CLOEXEC) = 3
read(3, "\177ELF...", 832) = 832
fstat(3, {st_mode=S_IFREG|0644, st_size=2592224, ...}) = 0
mmap(NULL, 2609472, PROT_READ, MAP_PRIVATE|MAP_DENYWRITE, 3, 0) = 0x...
mmap(0x..., 1343488, PROT_READ|PROT_EXEC, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, 3, 0x9d000) = 0x...
...
close(3)
# repeats for libgcc_s.so.1, libc.so.6, libm.so.6
```

**What this teaches:** `mmap` is how shared libraries are loaded — the loader
reads the ELF header (`read(...,832)`), `fstat`s the file for its size, then
`mmap`s its segments with the right protections (`PROT_READ|PROT_EXEC` for code,
`PROT_READ|PROT_WRITE` for data). The libraries are *demand-paged*: `mmap`
establishes the mapping, but actual pages fault in lazily on first access. Note
each library reuses **fd 3** — every open is followed by a `close`, so the
lowest free descriptor is always 3.

### Phase B — Our actual file I/O

This is the part the lab is really about — the last few lines of the trace:

```
openat(AT_FDCWD, "test.txt", O_RDONLY)  = 3
read(3, "hello from lab 1\n", 8191)     = 17
fstat(1, {st_mode=S_IFREG|0664, st_size=96, ...}) = 0
read(3, "", 8191)                       = 0      # EOF
close(3)                                = 0
+++ exited with 0 +++
```

| Syscall | What happened here |
|---------|--------------------|
| `openat(AT_FDCWD, "test.txt", O_RDONLY) = 3` | Path resolved relative to the cwd; kernel returns fd **3** |
| `read(3, ..., 8191) = 17` | libstdc++ pulls the whole file into its stream buffer in one read; **17** bytes returned |
| `read(3, "", 8191) = 0` | Second read returns 0 → end of file |
| `close(3) = 0` | `~ifstream` releases the fd; inode refcount drops |

### Observed differences from the lab's idealized example (important!)

1. **`read` returned 17 bytes, not 18.** `"hello from lab 1"` is 16 characters
   plus one `\n` = **17 bytes**. The lab text's `st_size=18` / `read = 18` was
   off by one. `stat` (Step 3) confirms the real size is 17.
2. **Buffer size is 8191, not 4096.** libstdc++'s `basic_filebuf` uses an
   8192-byte buffer (8191 usable here), not the 4096 the lab guessed. The exact
   read size is a runtime decision of the C++ library, not something we control.
3. **No `fstat(3)` on the input file.** The lab example shows `fstat(3, ...)` on
   the data file. In the real run, libstdc++ did **not** `fstat` `test.txt`
   before reading it — it just `read` until EOF. The only `fstat` in Phase B is
   `fstat(1, ...)` on **stdout (fd 1)**, which `std::cout` does to decide its
   own buffering mode. This is a great reminder that the higher-level library,
   not the kernel, decides which syscalls get issued.

---

## Step 3 — The inode journey

`openat` does **path resolution → inode lookup → permission check → fd
allocation**. We can see the inode the kernel resolved:

```bash
$ ls -i test.txt
11306272 test.txt

$ stat test.txt
  File: test.txt
  Size: 17        	Blocks: 8          IO Block: 4096   regular file
Device: 259,2	Inode: 11306272    Links: 1
Access: (0664/-rw-rw-r--)  Uid: ( 1000/ ratnesh)   Gid: ( 1000/ ratnesh)
Access: 2026-06-23 00:31:36 +0530
Modify: 2026-06-23 00:31:36 +0530
Change: 2026-06-23 00:31:36 +0530
 Birth: 2026-06-23 00:31:36 +0530
```

Observations tying back to the syscall trace:

- **Inode `11306272`** is the kernel's canonical identity for this file. The
  filename `test.txt` is just a directory entry pointing at it.
- **Size 17** matches the `read(...) = 17` from `strace` exactly.
- **Blocks: 8** = 8 × 512-byte units = 4096 bytes physically allocated, i.e. one
  4 KiB filesystem block — even though the file holds only 17 bytes. Files are
  allocated in whole blocks.
- **`Links: 1`** is the hardlink count stored in the inode; `close` decrements
  the in-kernel *open* reference, while `unlink` would decrement this on-disk
  link count.
- **Mode `0664`, Uid 1000** are exactly what the kernel checks the process's
  UID/GID against during the permission-check phase of `openat`.

---

## Step 4 — Kernel layers involved

```
        C++  std::ifstream / std::getline
                     |
        libstdc++ basic_filebuf  (8 KiB user-space buffer)
                     |
              read() syscall          <-- user / kernel boundary (this is what strace shows)
                     |
        VFS (Virtual Filesystem Switch)  -- generic file/inode/dentry objects
                     |
        Filesystem driver (ext4 / btrfs) -- maps file offset -> disk blocks
                     |
        Page cache    -- if pages are resident, served from RAM, NO disk I/O
                     |
        Block device driver -> physical disk  (only on a cache miss / cold read)
```

Two things this layering explains, confirmed by the trace:

- **One `read` syscall served the whole file.** The 17 bytes came back in a
  single `read`; the libstdc++ buffer means `std::getline` calls do **not** map
  1:1 to syscalls. User-space buffering collapses many logical reads into few
  syscalls.
- **The page cache makes repeat reads free.** Because we had just written
  `test.txt`, its single data block was already resident in the page cache, so
  the `read` was satisfied from RAM with no block-device I/O. A truly cold read
  (after `echo 3 > /proc/sys/vm/drop_caches`) would additionally trigger
  block-layer activity behind the same `read` syscall — invisible to `strace`,
  because `strace` only sees the syscall boundary, not what happens beneath it.

---

## Step 5 — Verify with `/proc`

To inspect the open fd *while the process is alive*, I used
[`reader_sleep.cpp`](reader_sleep.cpp), which keeps the file open for 3 seconds:

```bash
$ ./reader_sleep &        # prints its PID, then sleeps 3s
PID=100402 holding test.txt open for 3s...

$ ls -l /proc/100402/fd
lrwx------ ... 0 -> socket:[276238]
l-wx------ ... 1 -> .../br66es5bj.output       # stdout
l-wx------ ... 2 -> .../br66es5bj.output       # stderr
lr-x------ ... 3 -> /home/ratnesh/.../Lab1-Assignment/test.txt   # <-- our file, read-only

$ stat -L /proc/100402/fd/3
  File: /proc/100402/fd/3
  Size: 17  Blocks: 8  IO Block: 4096  regular file
  Device: 259,2  Inode: 11306272   Links: 1
```

**The key verification:** fd **3** is a symlink to `test.txt`, it is `r-x`
(opened `O_RDONLY`), and `stat -L` through it reports **inode `11306272`** — the
**same inode** we saw via `stat test.txt` in Step 3. This is concrete proof that
the per-process file descriptor (a small integer, here `3`) ultimately resolves,
via the kernel's open-file table → `struct file` → inode, to the one canonical
inode object on the filesystem.

---

## Reproduce everything

```bash
cd Lab1-Assignment
echo "hello from lab 1" > test.txt

# Steps 1-2
g++ -o reader reader.cpp
strace -e trace=openat,read,close,fstat,mmap ./reader 2> strace_filtered.txt

# Step 3
ls -i test.txt
stat test.txt

# Step 5
g++ -o reader_sleep reader_sleep.cpp
./reader_sleep &        # note the PID it prints
ls -l /proc/<PID>/fd
stat -L /proc/<PID>/fd/3
```

---

## Key Takeaways

- Every `std::ifstream` open ultimately becomes an **`openat`** syscall, which
  walks the VFS, resolves a **filename → inode**, checks permissions, and hands
  back the lowest free **file descriptor** (here, `3`).
- The **fd is a per-process handle**; the **inode is the kernel's canonical,
  filesystem-wide identity** of the file. `/proc/<PID>/fd/3` and `stat test.txt`
  reporting the *same inode number* is the direct evidence of this mapping.
- **The library, not the kernel, decides the syscall pattern.** libstdc++ used
  an 8 KiB buffer, read the file in a single `read`, and `fstat`ed *stdout*
  (not the input file) — none of which the lab's idealized trace predicted.
  Always trust the live `strace` over the textbook example.
- **`strace` exposes exactly the user/kernel boundary** — and *only* that
  boundary. The page cache, VFS dispatch, and block I/O all happen *beneath* the
  `read` syscall and are invisible to it; that invisibility is itself the lesson
  about where the abstraction line sits.
- The **page cache** means repeated reads of a warm file never touch the disk —
  they are served from RAM behind the same unchanged `read()` syscall.

---

### Reference

- Solution to `lab_sessions/lab_1.txt` (Advanced DBMS lab series).
- Tools: `g++` 13.3.0, `strace`, `stat`, `/proc` — Ubuntu 24.04, x86-64.
