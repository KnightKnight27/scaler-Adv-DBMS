# Read / Write File — Linux POSIX I/O walkthrough

A C++ program that takes one file through eight steps of the POSIX I/O
interface — `open → write → fsync → close → open → read → lseek → close` —
printing what the kernel is doing at each step. The goal isn't to show how
to write to a file (`std::ofstream` does that in one line); it's to expose
the kernel-side machinery underneath the syscalls: file descriptors, open
file descriptions, inodes, the page cache, file offsets, and durability via
`fsync()`.

## Build and run

```bash
g++ -std=c++17 -Wall -Wextra -O2 main.cpp -o main
./main
```

To see the actual syscalls the program issues:

```bash
strace -e trace=openat,read,write,close,fsync,lseek,fstat ./main
```

A representative transcript is in the next section, annotated phase-by-phase.

## What the kernel does, per syscall

| Call | What the kernel does |
|---|---|
| `open(path, flags, mode)` | Walks the path, finds (or creates with `O_CREAT`) the inode, checks permissions, allocates an open file description with the requested flags, installs it at the lowest free fd in this process's FD table, returns that fd. `O_TRUNC` truncates the file to size 0 *as part of the open*. |
| `write(fd, buf, n)` | Validates the fd, copies `n` bytes from user space into the **page cache** (in 4 KiB pages, marked dirty), extends the inode size and updates mtime/ctime, advances the open file description's offset. Returns the count accepted. **Data is not on disk yet.** |
| `fsync(fd)` | Forces every dirty page belonging to this inode (and the inode metadata itself) to the block device and waits for the device to acknowledge. After this returns 0, the data survives a crash. |
| `close(fd)` | Removes the fd from the process FD table, decrements the open file description's reference count, reclaims it if the count hits zero. **Does not imply fsync** — uncommitted dirty pages stay in the page cache until writeback. |
| `read(fd, buf, n)` | Reads up to `n` bytes from the page cache (issuing disk reads only on cache miss), copies them into the user buffer, advances the offset. Can return fewer than `n` bytes at EOF. |
| `lseek(fd, off, whence)` | Pure metadata update on the open file description. No disk I/O. `SEEK_SET` is absolute, `SEEK_CUR` is relative, `SEEK_END` is relative to file size. |

## The three-level FD model

This is the part that trips most people up. `open()` does **not** give you a
pointer to an inode. It gives you an index into a three-level structure:

```
   user process              kernel                          disk
   ────────────              ──────                          ────
   ┌──────────┐
   │  fd = 3  │ ──┐
   └──────────┘   │      Per-process FD table
                  └────► ┌─────┬──────────────────────┐
                         │  0  │ -> stdin             │
                         │  1  │ -> stdout            │
                         │  2  │ -> stderr            │
                         │  3  │ -> open file desc A  │ ──┐
                         └─────┴──────────────────────┘   │
                                                          │
                                Open file descriptions    │
                                (system-wide table)       │
                                ┌──────────────────────┐  │
                                │  desc A: offset=25,  │◄─┘
                                │   mode=O_WRONLY,     │
                                │   inode -> 7683476   │ ──┐
                                └──────────────────────┘   │
                                                           │
                                            In-memory inode│
                                            ┌──────────────┴───┐
                                            │ ino 7683476       │
                                            │ size=25, mode=644 │
                                            │ block list, times │
                                            └────────┬──────────┘
                                                     │ writeback
                                                     ▼
                                            ┌──────────────────┐
                                            │ on-disk inode +  │
                                            │ data blocks      │
                                            └──────────────────┘
```

Three consequences:

1. **`fd` is per-process.** Closing fd 3 here does not affect fd 3 in any
   other process. After `fork()` both parent and child see fd 3 *and both
   point at the same open file description* — so they share the file
   offset. That's why two processes writing to a shared fd after fork
   don't stomp on each other's offsets.
2. **The file offset lives in the open file description**, not the fd and
   not the inode. That's why closing and re-opening the file in `main.cpp`
   resets the offset to 0: same inode, brand-new open file description.
3. **The inode is the file's identity.** Renaming the file does not change
   its inode. Deleting the file while it is still open just removes the
   directory entry — the inode survives until the last fd is closed
   (this is how `/tmp` cleanup tricks and "deleted but still using space"
   `lsof` entries work).

## The page cache in one paragraph

Linux does not write to disk on `write()`. It writes to the **page cache**
— 4 KiB pages of RAM keyed by `(inode, offset)`. The pages get marked
dirty. A kernel thread periodically pushes dirty pages to the block
device. The page cache is also what makes Phase 6's read effectively free:
the pages we just wrote in Phase 2 are hot, so `read()` is a memcpy from
RAM to RAM. This is why `fsync()` matters — without it, "write returned
success" only guarantees "the bytes reached the cache."

## Representative `strace` transcript

```
openat(AT_FDCWD, "test.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
write(3, "I am writing to this file", 25)                    = 25
fsync(3)                                                     = 0
close(3)                                                     = 0
openat(AT_FDCWD, "test.txt", O_RDONLY)                       = 3
read(3, "I am writing to this file", 255)                    = 25
lseek(3, 0, SEEK_CUR)                                        = 25
lseek(3, 0, SEEK_SET)                                        = 0
read(3, "I am ", 5)                                          = 5
close(3)                                                     = 0
```

- `AT_FDCWD` means "resolve relative to the calling process's cwd".
- The fd is `3` because 0/1/2 are stdin/stdout/stderr.
- The second `openat` returns `3` again — the kernel hands out the lowest
  free fd, and we just closed fd 3.
- The second `read` of 255 bytes returns only 25 — that's all the file
  contains. `read()` returning less than requested is normal, not an error.
- `lseek` returns the new offset; `SEEK_CUR` with offset 0 is the idiom for
  "tell me where I am".

## Flag combinations worth knowing

| Flags | Effect |
|---|---|
| `O_CREAT \| O_WRONLY \| O_TRUNC` (this program) | Create if missing; truncate to 0; open for writing. The "give me a fresh file" idiom. |
| `O_CREAT \| O_WRONLY \| O_APPEND` | Each `write()` is atomic with respect to end-of-file. Removes the lost-write race in concurrent log writers. |
| `O_CREAT \| O_EXCL` | Fail with `EEXIST` if the file already exists. Canonical safe-create; used for lock files and PID files. |
| `O_RDONLY` (no `O_CREAT`) | Fail with `ENOENT` if the file is missing. |
| `O_DIRECT` | Bypass the page cache, DMA straight into the user buffer. Used by databases that manage their own buffer pool. Buffers must be page-aligned. |
| `O_SYNC` | Every `write()` returns only after data hits disk — equivalent to `write` + `fsync` on every call. Slow; explicit `fsync` at safe checkpoints is almost always better. |

## Glossary

- **System call** — boundary between user space and the kernel. Entered via
  the `syscall` instruction; the user process is paused while the kernel
  runs on its behalf.
- **File descriptor (fd)** — a small non-negative integer that indexes into
  the calling process's FD table. 0/1/2 are stdin/stdout/stderr by
  convention.
- **Open file description** — the kernel object that holds the flags and
  the current file offset. Shared across `fork()` and `dup()`; one per
  `open()` call.
- **Inode** — the on-disk (and in-memory cached) record of a file's
  metadata and its block list. A file's identity.
- **Page cache** — kernel's RAM-resident cache of file data, keyed by
  `(inode, page-aligned offset)`. Where `write()` actually lands and where
  `read()` usually comes from.
- **fsync** — block until this inode's dirty data and metadata are durable
  on the storage device.
