# Lab 1 — C++ File I/O Traced with `strace`: the inode → VFS → page cache → syscall journey

## 1. Goal

The aim of this lab is to look *through* a simple C++ program and watch what
actually happens when it asks the operating system to write and read a file.
We write a program that uses raw POSIX system calls (`open`, `write`, `lseek`,
`read`, `fsync`, `fstat`, `close`, `unlink`), run it under `strace`, and then
trace every call down from user space into the kernel — across the VFS, the
ext4 filesystem, the page cache, and finally the block layer and disk. The
recurring question is: *where do my bytes really live at each instant, and when
do they become durable?*

## 2. Files in this lab

| File | What it is |
|------|-----------|
| `file_io_demo.cpp` | The C++17 program performing the syscall sequence. |
| `run_strace.sh` | Compiles and runs the program under `strace` (Linux only). |
| `strace_output.txt` | An illustrative sample trace captured on Linux/ext4. |
| `README.md` | This report. |

Build and run:

```sh
g++ -std=c++17 file_io_demo.cpp -o file_io_demo
./run_strace.sh        # produces strace_output.txt on Linux
```

## 3. The layers a `read()` / `write()` passes through

```
 +-------------------------------------------------------------+
 |  USER SPACE                                                 |
 |    C++ program: write(fd, buf, n)                           |
 |        |                                                    |
 |    glibc wrapper (libc.so): loads args, sets syscall number |
 |        |  syscall / SYSCALL instruction (trap into kernel)  |
 +--------|----------------------------------------------------+
          v
 +-------------------------------------------------------------+
 |  KERNEL SPACE                                               |
 |                                                             |
 |  System call dispatch  (sys_write / sys_read)               |
 |        |                                                    |
 |  fd table  ->  open file description  ->  inode             |
 |        |                                                    |
 |  VFS (Virtual File System)                                  |
 |    - dentry cache  (name -> inode lookups)                  |
 |    - inode object  (metadata, owns the file)                |
 |        |                                                    |
 |  Filesystem driver (ext4): file_operations callbacks        |
 |        |                                                    |
 |  PAGE CACHE  (per-inode pages of file data in RAM)          |
 |    - read hit   -> copy page -> user buffer (NO disk I/O)   |
 |    - write      -> copy user buffer -> page, mark DIRTY     |
 |    - writeback / fsync -> push dirty pages downward         |
 |        |                                                    |
 |  Block layer + I/O scheduler (bio requests, merging)        |
 |        |                                                    |
 |  Device driver                                              |
 +--------|----------------------------------------------------+
          v
 +-------------------------------------------------------------+
 |  HARDWARE: storage device (SSD / HDD)                       |
 +-------------------------------------------------------------+
```

### 3.1 User space → glibc wrapper → syscall trap

When the program calls `write(fd, buf, n)`, it is not calling the kernel
directly. It calls a thin wrapper inside the C library (`libc.so`). The wrapper
places the system-call number for `write` into a register, the arguments into
the argument registers, and executes the architecture's trap instruction
(`syscall` on x86-64). That instruction is the boundary: the CPU switches from
unprivileged user mode to privileged kernel mode and jumps to a fixed kernel
entry point. This is the single most important transition in the whole journey,
because it is where the program stops being in control and hands the request to
the kernel. `strace` works precisely by intercepting at this boundary (via
`ptrace`), which is why every line in `strace_output.txt` corresponds to one
crossing of this trap.

### 3.2 Syscall dispatch → fd → open file description → inode

Inside the kernel, the dispatcher routes the call to `sys_write`. The first
thing it does is translate the integer `fd` into a real object. Each process
has a **file descriptor table**; entry `fd=3` points to an **open file
description** (which holds the current offset and the access mode), and that in
turn points to the **inode**. So the small number `3` that we pass around in
user space is really a three-hop indirection ending at the file's inode.

### 3.3 VFS, the dentry cache, and the inode's role

The **VFS** is an abstraction layer that lets every filesystem (ext4, xfs,
tmpfs, NFS, …) present the same interface to the rest of the kernel. It defines
generic objects — `inode`, `dentry`, `file`, `super_block` — and a table of
function pointers (`file_operations`) that the concrete filesystem fills in.

The **inode** is the heart of a file. The file's *name* is not part of it; the
inode stores the file's metadata and the map to its data: size, owner,
permission bits, timestamps, link count, and pointers (in ext4, an extent tree)
to the actual data blocks on disk. A name in a directory is just a **directory
entry (dentry)** that maps a string to an inode number. This is why two names
can refer to one file (hard links): both dentries point at the same inode, and
the inode's `st_nlink` counts them. Our `fstat` step prints exactly these inode
fields — inode number, size, block count, link count — to make the inode
concrete.

The **dentry cache** speeds up path resolution. Walking `/home/user/lab1_data.bin`
means resolving each component to an inode; caching `name → inode` results
avoids re-reading directory blocks from disk on every `open`. When we call
`open("lab1_data.bin", ...)`, the VFS resolves the name against the current
directory through this cache and binds the resulting inode to a new fd.

### 3.4 The page cache: where the data actually sits

The kernel does **not** talk to the disk on every `read`/`write`. Instead each
inode owns a set of in-RAM pages — the **page cache**.

- On **write**: the bytes are copied from our user buffer into a page in the
  cache, and that page is flagged **dirty** (modified but not yet on disk). The
  `write` syscall returns immediately. This is *buffered* I/O: durability is
  deferred.
- On **read**: if the requested page is already in the cache (a *cache hit*),
  the kernel copies it straight back to the user buffer with **no disk access**.
  Only on a *miss* does it issue a real read request downward and block until
  the data arrives.

Dirty pages are flushed later by kernel **writeback** threads — driven by
timers, memory pressure, or dirty-ratio thresholds. The danger is obvious: if
the machine crashes between the `write` returning and writeback running, the
data is lost. That is what **`fsync`** fixes: it blocks the caller until every
dirty page of the file *and* the relevant inode metadata have been pushed all
the way to stable storage. Databases call `fsync`/`fdatasync` at transaction
commit for exactly this guarantee.

### 3.5 Block layer → device → disk

Below the filesystem, page-cache flushes become **bio** requests handed to the
block layer. The I/O scheduler may merge and reorder them for efficiency, then
the device driver programs the hardware. Only here do bytes physically reach the
SSD or HDD platter.

## 4. Mapping each traced syscall to kernel behaviour

These map directly to the lines in `strace_output.txt`.

- **`openat(AT_FDCWD, "lab1_data.bin", O_RDWR|O_CREAT|O_TRUNC, 0644) = 3`**
  VFS resolves the name via the dentry cache; because of `O_CREAT` it allocates
  an inode and a directory entry if needed; `O_TRUNC` resets the size to 0. The
  return value `3` is the new fd. (`open` in C is the `openat` syscall on Linux.)

- **`write(3, "Hello from the page cache!...", 85) = 85`**
  85 bytes copied into a dirty page of the inode's page cache; offset advances
  to 85. No disk I/O yet. Return value is the byte count accepted.

- **`lseek(3, 0, SEEK_SET) = 0`**
  Pure kernel bookkeeping: the offset in the open file description is set to 0
  so the next read starts at the beginning. Touches neither cache nor disk.

- **`read(3, "Hello from the page cache!...", 255) = 85`**
  Served from the page cache (the page we just wrote is warm), so it is a cache
  *hit*: data copied from kernel page to user buffer with no disk access. Returns
  85 because that is all the file holds, even though we asked for 255.

- **`fsync(3) = 0`**
  Forces the dirty page and inode metadata down through the block layer to the
  device; blocks until the device confirms. After this the data is durable.

- **`fstat(3, {st_ino=11386660, st_size=85, st_blocks=8, st_nlink=1, ...}) = 0`**
  Reads the inode's metadata (cached in memory) — inode number, size, allocated
  512-byte blocks, link count. This demonstrates that metadata lives in the
  inode, separate from the file's data bytes.

- **`close(3) = 0`**
  Releases the fd and its open file description. The inode persists on disk; only
  our handle is gone.

- **`unlink("lab1_data.bin") = 0`**
  Removes the directory entry and decrements the inode's link count. When the
  count hits 0 and no process still holds the file open, ext4 frees the inode and
  its data blocks.

## 5. Buffered vs. direct I/O — why it matters

Everything above describes **buffered** I/O through the page cache, which gives
fast reads on warm data and lets writes return before they hit disk. The cost is
that durability requires an explicit `fsync`. **Direct** I/O (`O_DIRECT`)
bypasses the page cache and transfers straight to the device. It avoids
double-buffering and is used by systems — typically databases — that maintain
their own buffer pool and want full control over caching and durability. We
chose buffered I/O plus an explicit `fsync` here because it makes the page-cache
vs. durability distinction visible in the trace, which is the whole point of the
lab.

## 6. Conclusion

`strace` turns an opaque program into a readable sequence of kernel requests.
Following the chain **inode → VFS → page cache → syscall** shows that a file is
really three separable things: a *name* (dentry), its *metadata and block map*
(inode), and its *data* (pages in the cache, eventually on disk). The page cache
is what makes I/O fast, and `fsync` is what makes it safe — and seeing both in a
trace is the clearest way to understand the trade-off every storage system makes.
