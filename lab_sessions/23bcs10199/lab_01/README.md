# Lab 1 — Linux File I/O: Kernel Journey via strace

**Student:** Indrajeet Yadav | **Roll No:** 23BCS10199

---

## Objective

Understand what happens under the hood when a C++ program opens, writes, reads, and closes a file — tracing every step from the user-space `syscall` boundary down through the VFS, page cache, inode, and into physical storage.

---

## Build & Run

```bash
g++ -std=c++17 -Wall -Wextra -O2 file_io.cpp -o file_io
./file_io
```

To see the actual syscalls issued at the kernel boundary:

```bash
strace -e trace=openat,read,write,close,fsync,lseek,fstat ./file_io
```

---

## Program Structure — Nine Phases

The program walks a file through a complete lifecycle:

| Phase | Syscall | What happens |
|-------|---------|-------------|
| 1 | `open(O_CREAT\|O_WRONLY\|O_TRUNC)` | Path resolution → inode allocation → FD table install |
| 2 | `write()` | User buffer → page cache (dirty pages, NOT disk) |
| 3 | `fsync()` | Flush dirty pages to storage, wait for device ACK |
| 4 | `close()` | Remove fd, decrement description ref count |
| 5 | `open(O_RDONLY)` | Fresh description, offset=0, same inode |
| 6 | `read()` | Page cache hit → memcpy to user buffer |
| 7 | `lseek()` | Pure metadata — moves offset inside description |
| 8 | `fstat()` | Read inode metadata from VFS icache |
| 9 | `close()` | Final cleanup |

---

## The Three-Level FD Model

This is the most misunderstood part of POSIX I/O. There are three distinct objects:

```
   User Process                 Kernel                         Disk
   ────────────                 ──────                         ────
   ┌──────────┐
   │  fd = 3  │──┐
   └──────────┘  │     Per-Process FD Table
                 └──►  ┌─────┬──────────────────────┐
                        │  0  │ → stdin               │
                        │  1  │ → stdout              │
                        │  2  │ → stderr              │
                        │  3  │ → open file desc A   │──┐
                        └─────┴──────────────────────┘  │
                                                         │
                               Open File Description     │
                               (system-wide table)       │
                               ┌────────────────────┐    │
                               │ desc A:            │◄───┘
                               │  offset = 46       │
                               │  flags  = O_RDONLY │
                               │  inode → 7683476   │──┐
                               └────────────────────┘  │
                                                        │
                                       In-Memory Inode  │
                                       ┌────────────────┴──┐
                                       │ ino 7683476        │
                                       │ size=46, mode=0644 │
                                       │ block list, times  │
                                       └──────────┬─────────┘
                                                  │ writeback
                                                  ▼
                                       ┌─────────────────────┐
                                       │ on-disk inode +      │
                                       │ data blocks          │
                                       └─────────────────────┘
```

### Three critical consequences

1. **`fd` is per-process.** Closing fd 3 in this process doesn't affect fd 3 in any other process. After `fork()`, both parent and child point to the **same open file description** — they share the offset, which is why two processes writing to the same log after a fork can interleave lines.

2. **The file offset lives in the open file description, not the inode.** Calling `close()` and re-`open()`-ing the file creates a brand-new description with offset=0. Two concurrent `open()` calls on the same file give two independent offsets but the same inode — so writes from both can coexist.

3. **The inode is the file's identity.** Renaming or moving a file (even across directories on the same filesystem) does not change the inode number. If you delete a file while it's open (e.g., a running process has it), the directory entry disappears but the inode (and all its data) survive until the last fd is closed. This is how `lsof | grep deleted` shows "deleted but still consuming disk" files.

---

## The Page Cache

Linux does not write to disk on `write()`. It writes to the **page cache** — a kernel-managed pool of 4 KiB RAM pages, each keyed by `(inode, page-aligned-offset)`.

```
write(fd, buf, n)
    │
    ▼
Page Cache (RAM)   ←── dirty pages (marked for writeback)
    │
    │  (async writeback — kernel thread, ~30 seconds by default)
    ▼
Block Device (SSD/HDD)
```

Consequences:
- `write()` returning success does NOT mean data is on disk.
- A power failure after `write()` but before writeback **loses the data**.
- `fsync()` forces all dirty pages for this inode to the device and waits for an ACK. This is why every database (PostgreSQL, SQLite, MySQL) calls `fsync` at transaction commit.
- Sequential `read()` after `write()` is essentially free — the pages are already in RAM. No disk I/O occurs on a cache hit.

---

## Annotated `strace` Transcript

```
# Phase 1 — open for writing
openat(AT_FDCWD, "lab1_test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
#         │                                                          └─ fd assigned
#         └─ resolve relative to cwd (AT_FDCWD)

# Phase 2 — write into page cache
write(3, "Hello from Lab 1 ...", 46)    = 46
#     │   └─ user buffer               └─ bytes accepted into cache, NOT disk

# Phase 3 — force to disk
fsync(3)                                = 0
#                                         └─ 0 = success, data on disk

# Phase 4 — release write fd
close(3)                                = 0

# Phase 5 — re-open for reading
openat(AT_FDCWD, "lab1_test.txt", O_RDONLY) = 3
#                                              └─ kernel recycled fd 3

# Phase 6 — read from page cache
read(3, "Hello from Lab 1 ...", 511)    = 46
#                                 └─ asked for 511, got 46 (EOF) — normal

# Phase 7 — query current offset (SEEK_CUR + 0)
lseek(3, 0, SEEK_CUR)                  = 46
lseek(3, 0, SEEK_SET)                  = 0    # rewind
read(3, "Hello fro", 10)               = 10   # first 10 bytes again

# Phase 8 — read inode metadata
fstat(3, {st_mode=S_IFREG|0644, st_size=46, ...}) = 0

# Phase 9 — final close
close(3)                                = 0
```

Key observations from the trace:
- `AT_FDCWD` means "resolve the path relative to the process's current working directory."
- The fd is `3` because 0/1/2 are already taken by stdin/stdout/stderr.
- The second `openat` returns `3` again — the kernel always hands out the **lowest free fd**, and we just closed 3.
- `read()` returning fewer bytes than requested is never an error when it happens at EOF. Always check the return value.
- `lseek` with `SEEK_CUR` and offset 0 is the standard idiom for "tell me where I am."

---

## Kernel Layers

```
C++ code (file_io.cpp)
    │  open() / read() / write() / close() / fsync() / lseek()
    │
    ▼
glibc (thin wrapper — adjusts calling convention, sets errno)
    │
    │  syscall instruction (enters kernel mode)
    ▼
VFS — Virtual Filesystem Switch (fs/vfs.c in the Linux source)
    │  path_lookup(), inode_permission(), alloc_file()
    │
    ▼
Filesystem Driver (fs/ext4/ or fs/btrfs/ or fs/xfs/)
    │  ext4_file_read_iter(), ext4_file_write_iter()
    │
    ▼
Page Cache (mm/filemap.c)
    │  __generic_file_read_iter(), generic_perform_write()
    │
    ├── Cache HIT  → memcpy to/from user buffer, done
    │
    └── Cache MISS → submit_bio() → I/O scheduler → block driver → SSD/HDD
```

---

## Flag Combinations Worth Knowing

| Flags | Effect |
|-------|--------|
| `O_CREAT \| O_WRONLY \| O_TRUNC` | Create or truncate. The standard "fresh file" idiom. |
| `O_CREAT \| O_WRONLY \| O_APPEND` | Each `write()` is atomic with respect to EOF. Used for concurrent log writers to avoid lost lines. |
| `O_CREAT \| O_EXCL` | Fail with `EEXIST` if the file already exists. The safe lock-file creation pattern. |
| `O_RDONLY` | Read-only. `ENOENT` if the file doesn't exist (no `O_CREAT`). |
| `O_DIRECT` | Bypass the page cache entirely; DMA straight into the user buffer. Used by databases managing their own buffer pool (e.g., PostgreSQL with `o_direct` on WAL). Buffers must be page-aligned. |
| `O_SYNC` | Every `write()` blocks until data is durable — equivalent to calling `write + fsync` for every call. Slow; explicit `fsync()` at safe points is almost always better. |

---

## Key Takeaways

1. Every `open()` call ultimately executes `openat` which traverses the VFS, resolves an inode, and returns an index into the process-private FD table.
2. `write()` copies data to the **page cache** (RAM). It does **not** hit the disk.
3. `fsync()` is the only guarantee that data survives a crash.
4. `close()` frees the fd and releases the description, but does **not** flush the page cache.
5. The **file offset** lives in the open file description (not the inode), so re-opening a file resets the offset independently of other opens.
6. The **page cache** is the reason that reading a just-written file has zero disk I/O — the data is already in RAM.
7. `strace` exposes the exact kernel boundary and lets you verify every claim above.

---

## Glossary

| Term | Definition |
|------|-----------|
| **System call** | The CPU instruction (`syscall`) that transfers control from user space to the kernel. The process pauses; the kernel runs on its behalf. |
| **File descriptor (fd)** | A small non-negative integer indexing into the process's FD table. 0/1/2 = stdin/stdout/stderr by POSIX convention. |
| **Open file description** | The kernel struct (`struct file`) that stores flags, the file offset, and a pointer to the inode. Created by `open()`; shared across `fork()` and `dup()`. |
| **Inode** | The on-disk (and VFS-cached) record that stores a file's metadata and the map of its data blocks. The file's identity — independent of its name. |
| **Page cache** | Kernel-managed RAM cache of file data, indexed by `(inode, page-offset)`. Where `write()` actually lands and where `read()` usually reads from. |
| **fsync** | Block until this inode's dirty data AND metadata are durable on the storage device. The database commit guarantee. |
| **VFS** | Virtual Filesystem Switch — the kernel abstraction layer that lets the same `read()`/`write()` calls work on ext4, btrfs, tmpfs, procfs, etc. |
