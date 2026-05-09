# Lab 1 — File I/O Using Raw Syscalls

**Name:** Krritin Keshan
**College ID:** 24bcs10122
**Course:** Advanced DBMS
**Date:** May 9, 2026

---

## What I built

A small C++ program (`fileio.cpp`) that reads a file and writes a new one **without using any C/C++ library I/O**. That means:

- No `<stdio.h>` (so no `printf`, `fopen`, `fread`)
- No `<iostream>` (so no `cout`, `cin`)
- No `<fstream>` (so no `ifstream`, `ofstream`)

The only things the program touches for I/O are the four POSIX syscall wrappers: `open`, `read`, `write`, `close` — straight from `<unistd.h>` and `<fcntl.h>`. These are not "library functions with hidden buffers"; they are tiny pieces of glue that put your arguments in registers and execute the kernel's `SYSCALL` instruction.

---

## Files in this lab

| File | What it is |
|------|------------|
| `fileio.cpp` | The program. All I/O goes through raw syscalls. |
| `input.txt`  | The source file the program reads. |
| `output.txt` | The file the program produces (regenerated on every run). |
| `README.md`  | This document. |

---

## How to build and run

```bash
g++ -O2 -o fileio fileio.cpp
./fileio
```

No flags, no libraries, no special setup. Works on Linux out of the box and also on macOS (the POSIX calls behave the same — the syscall numbers underneath differ but that does not matter at the source level).

What you should see on stdout when you run it:

```
[1/6] opening input.txt for reading
      got fd = 3
[2/6] reading bytes from input.txt
      bytes read = ...
      ----- begin file content -----
      <contents of input.txt>
      ----- end file content -----
[3/6] closing input.txt
[4/6] opening output.txt for writing
      got fd = 3
[5/6] writing content to output.txt
      bytes written = ...
[6/6] closing output.txt
[ok]  all done — check output.txt
```

> Notice that the second `open` also returned `fd = 3`. That is on purpose — `close` released the slot, so the next `open` picks the smallest free integer, which is 3 again.

---

## What actually happens inside each syscall

I find it easier to follow the path from "I called `open`" to "the bits arrive on the SSD" if it is laid out as a stack:

```
        my C++ code  (user space)
              │
              │  SYSCALL instruction (the only way out of user space)
              ▼
   Linux kernel — syscall dispatcher
              │
              ▼
     VFS  (Virtual File System)        ← uniform API on top of every fs type
              │
              ▼
  filesystem driver (ext4 / xfs / apfs / tmpfs …)
              │
              ▼
   block layer  (I/O scheduler, request queue)
              │
              ▼
   device driver  (NVMe / SATA / virtio-blk …)
              │
              ▼
       physical hardware  (SSD NAND or HDD platter)
```

Every single read/write call from a normal program drops through that whole stack. Most of the time we never see it because libc and the page cache make it look fast and simple.

### `open("input.txt", O_RDONLY)`
- The kernel **walks the path**. Each directory component is looked up in the dentry cache; on a miss it reads the directory blocks from disk.
- Once the inode is found, **permissions are checked** against the calling process's UID/GID.
- The kernel **allocates a `struct file`** in kernel memory and assigns the smallest unused **file descriptor** in our process.
- The fd is returned to user space — just an integer.

### `read(fd, buf, n)`
- The current **file offset** is read (starts at 0 right after open).
- The kernel checks the **page cache**:
  - **Hit:** the requested pages are already in RAM — they get copied straight to our buffer. This is roughly nanoseconds.
  - **Miss:** a block I/O request is queued; our process sleeps; the device driver triggers a DMA transfer; an interrupt wakes us; the freshly filled pages are then copied to our buffer.
- The offset is **advanced** by however many bytes we got.
- Returns the byte count, `0` at EOF, or `-1` on error.

### `close(fd)`
- The kernel **decrements the refcount** on the `struct file`. When it hits zero, the description is freed and the fd integer goes back into the free pool.
- It does **not** flush dirty pages to disk. That happens via writeback (asynchronous) or an explicit `fsync()`.

### `open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644)`
- `O_CREAT` — if no inode exists, the kernel allocates one, journals the change (on ext4 / xfs), and adds a directory entry in the parent directory.
- `O_TRUNC` — if the file already exists, its size is set to 0 and its data blocks are released.
- `0644` — permissions: owner can read+write, group and others can only read.

### `write(fd, buf, n)`
- The user buffer is **copied into a kernel page** which is now marked **dirty**.
- The inode's size and mtime are updated in memory.
- `write()` **returns immediately** — the data is NOT yet on the disk. This is the standard write-back model.
- A kernel flusher thread will write the dirty pages later (default ~30 s, controlled by `vm.dirty_expire_centisecs`), or sooner if memory pressure builds.
- If you need a hard guarantee that the bytes hit the platter / NAND, call `fsync(fd)` before `close()`.

---

## Why no library I/O?

This is the question the lab is really asking, and I think the cleanest way to see it is with a layer diagram:

| Layer | What it adds |
|-------|--------------|
| `iostream` / `fopen` | User-space buffering, format parsing, locale handling, thread safety |
| `libc` syscall wrapper | A few lines of glue around the kernel `SYSCALL` instruction |
| **kernel SYSCALL** ← *this is what we're using* | The actual interface to the OS |

When you call `printf`, the bytes you wanted to print sit in libc's buffer for a while and only get flushed to the kernel later. That is fine for ergonomics, but it means the syscalls don't happen exactly when you think they do, and the relationship between your code and the kernel is hidden.

By going straight to `read` / `write`, every byte transfer is **one syscall, right now**. That makes the OS interaction obvious — which is exactly what we want when we are studying it.

---

## A few things I learnt while doing this

1. **A file descriptor is just an integer**, and the integers are reused. As soon as I closed the input file the same fd number was handed back when I opened the output file — small detail but it really hammers home what fds are.
2. **`write()` returning is not the same as the data being on disk.** It only means the bytes made it into the page cache. If you pull the power cord the next second, you can lose them. `fsync()` is the call that actually waits for the device.
3. **`read()` of a small file is almost always a cache hit** the second time around, because the kernel keeps recently-used pages in RAM. You can see this by running the program twice in a row — the second run is noticeably faster even though the file did not change.
4. **You really do not need much of libc to do useful I/O.** The whole program — open, read, write, close, plus a tiny number-to-string helper so I could print the byte count — is well under 200 lines.

---

## Syscall numbers (Linux x86-64, for reference)

| Syscall  | Number |
|----------|--------|
| `read`   | 0 |
| `write`  | 1 |
| `open`   | 2 |
| `close`  | 3 |
| `openat` | 257 |

The C functions in `<unistd.h>` ultimately resolve to these numbers in the `rax` register before issuing the `SYSCALL` instruction.

---

## Glossary of terms touched in this lab

- **VFS** — Virtual File System; the kernel's uniform layer over every filesystem type.
- **inode** — the per-file metadata node (size, perms, block pointers).
- **dentry cache** — kernel cache of directory entries that speeds up path lookups.
- **page cache** — the kernel's in-RAM cache of file contents.
- **dirty page** — a page that has been modified in cache but not yet flushed to disk.
- **writeback** — the asynchronous mechanism that flushes dirty pages.
- **fd table** — a per-process table mapping integer file descriptors to `struct file`s.
- **journal** — write-ahead log used by ext4 / xfs to keep filesystem state consistent across crashes.
