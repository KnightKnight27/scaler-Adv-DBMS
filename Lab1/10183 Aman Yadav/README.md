# Lab 1: Raw File I/O with POSIX System Calls

**Role Number:** `10183`
**Name:** `Aman Yadav`

---

## Goal

Write a small C++ program that copies one file to another using only the
POSIX system-call interface (`open`, `read`, `write`, `close`) — no
`<iostream>`, no `<fstream>`, no `<cstdio>`. The point is to see the kernel
boundary clearly: what call does what, what each return value means, and how
the data physically moves from disk to user memory.

## Files

| File | Role |
|---|---|
| `file_syscalls.cpp` | The program — seeds `input.txt` if missing, copies it to `output.txt`, prints byte count |
| `input.txt` | Sample input (auto-created on first run) |
| `output.txt` | The copied output |

## Build & Run

```bash
g++ -std=c++17 -O2 -Wall -Wextra file_syscalls.cpp -o file_syscalls
./file_syscalls
```

Expected output:

```
Copy finished. Bytes written: 271
```

(The exact byte count depends on the contents of `input.txt`.)

---

## Design Notes

A few decisions worth calling out:

- **16 KB buffer.** Big enough that the syscall count stays low for any
  reasonable file, small enough to stay comfortably inside L1/L2 cache. A
  larger buffer doesn't help once the dominant cost moves from syscall entry
  to data copy.
- **Short-write loop.** `write()` is allowed to return fewer bytes than
  requested (signals, full pipe buffer, partial disk write on certain FS
  configurations). The helper `write_full()` re-invokes `write()` until the
  whole buffer is out, and retries on `EINTR`.
- **`posix_fadvise(POSIX_FADV_SEQUENTIAL)`.** A cheap hint to the Linux page
  cache that we'll read this file front-to-back; the kernel then does more
  aggressive read-ahead. The call is wrapped in `#ifdef` because macOS does
  not define it.
- **No standard-library I/O.** Even error reporting goes through `write()` on
  `STDERR_FILENO`. A tiny `utoa` helper turns the byte count into ASCII so
  the success message can also avoid `printf`.

---

## Concept Walkthrough

### 1. File descriptors

A file descriptor is just a small non-negative integer. Inside the kernel it
indexes into the calling process's file-descriptor table; each entry there
points to a kernel-side *open file description* that holds the current
offset, the open flags, and a reference to the underlying inode. Three
descriptors are pre-wired at process start: `0` (stdin), `1` (stdout),
`2` (stderr). `open()` always hands back the lowest free slot, which is why
closing `0` and then opening a file gives that file FD `0`.

A descriptor is a kernel resource; if you forget to `close()` it, the kernel
keeps the open file description and the inode pinned until your process
exits. On long-lived processes this is a leak.

### 2. `strace` (and `dtruss` on macOS)

`strace` prints every system call a process makes, with arguments and return
values. It's the cleanest way to verify that the source code you wrote
matches the kernel calls actually being issued.

```bash
strace -e trace=openat,read,write,close ./file_syscalls
```

Expected pattern:

```
openat(AT_FDCWD, "input.txt", O_RDONLY) = 3
openat(AT_FDCWD, "output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 4
read(3, "Lab 1 - raw POSIX...", 16384) = 271
write(4, "Lab 1 - raw POSIX...", 271)   = 271
read(3, "", 16384)                      = 0
close(3) = 0
close(4) = 0
```

The `read(... ) = 0` line is end-of-file — not an error, just an empty
result. On macOS the equivalent tool is `dtruss` (needs `sudo` and SIP
considerations).

### 3. Inodes

Every file on a UNIX-like filesystem is identified internally by an inode
number, not by its name. The inode is a fixed-size on-disk record holding:

- file type and mode bits
- owner UID and group GID
- size in bytes
- link count
- access / modify / change timestamps
- pointers (direct + indirect, or extents) to the data blocks

The filename lives in the parent directory entry — a directory is just a
file whose contents are name-to-inode mappings. That's why `ln` can create
a second name for the same file: both names point at the same inode.

### 4. The journey of a `read()`

1. Program calls `read(fd, buf, n)`. CPU switches into kernel mode.
2. Kernel finds the open file description from the FD table; checks the
   current offset.
3. Kernel asks the page cache: "do I already have the relevant file pages?"
4. **Cache hit:** kernel `memcpy`s straight from a page-cache page into the
   user buffer.
5. **Cache miss:** kernel queues a block I/O request, the storage driver
   programs a DMA transfer, the disk fills a free page-cache page directly
   over the bus, and an interrupt tells the kernel the page is ready.
6. Kernel copies from the (now-populated) page-cache page into the user
   buffer, advances the FD's offset, returns the byte count.
7. A return value of `0` means EOF; a return value smaller than `n` is
   normal for pipes/sockets/short reads and is not an error.

### 5. Pages and the page cache

Linux manages physical RAM in 4 KB *pages* (other architectures use larger
sizes or huge pages). When a file is read, its contents land in
page-cache pages keyed by `(inode, offset)`. The next read of the same
range — by this process or any other — is just a `memcpy` from those pages.
This is why the *first* run of a query is slow and the *second* is fast,
even on a "cold" disk.

### 6. Device drivers

User code never talks to disks. The path is:

```
program -> syscall -> VFS -> filesystem (ext4/apfs/xfs)
        -> block layer -> driver (NVMe / AHCI / virtio-blk) -> hardware
```

Each layer has a stable interface to the one above; the driver is the only
piece that knows the hardware-specific command set.

### 7. DMA — Direct Memory Access

Older systems had the CPU read each byte from a disk register and store it
to RAM ("programmed I/O"). DMA flips that: the storage controller is given
a destination RAM address and the size of the transfer, and it moves the
data straight into RAM over the system bus while the CPU does other work.
The CPU is only involved at the start (set up the descriptor) and the end
(handle the completion interrupt), making large reads almost free in CPU
terms.

### 8. Page-cache eviction

The page cache grows until something else needs the RAM. When that happens,
Linux uses an Active/Inactive two-list LRU approximation:

- pages on the **inactive** list that are *clean* (read but not modified)
  can be dropped instantly — they're identical to disk
- pages that are *dirty* (written but not yet flushed) are first sent to
  disk by the writeback machinery, then evicted

This is the mechanism behind the famous "first run slow, repeat run fast,
then slow again after another workload runs": the workload between your
runs evicted the relevant pages.

---

## Quick Verification

```bash
# byte counts must match
wc -c input.txt output.txt

# md5/sha must match
md5sum input.txt output.txt   # Linux
md5    input.txt output.txt   # macOS

# trace the syscalls
strace -c ./file_syscalls    # syscall summary table
```

## Summary

- POSIX I/O (`open`/`read`/`write`/`close`) is the thinnest portable layer
  above the kernel; everything in the C/C++ standard library eventually
  funnels into these calls.
- Short reads and short writes are normal — always loop.
- The page cache is the single biggest reason file I/O feels fast: a hit is
  a `memcpy`, a miss is a disk round-trip.
- DMA keeps the CPU out of the data-copy path between disk and RAM.
- `strace` (or `dtruss`) is the fastest way to see what your program is
  actually asking the kernel to do.
