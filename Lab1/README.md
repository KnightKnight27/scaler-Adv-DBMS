# Lab 1: Raw File I/O with POSIX System Calls

**Role Number:** `24BCS10406`
**Name:** `Manasvi Sabbarwal`
**Platform tested on:** macOS (Darwin 25.3.0, aarch64), Apple clang 17

---

## Goal

Write a small C++ program that copies one file to another using **only** the
POSIX system-call interface — `open`, `read`, `write`, `close`. No
`<iostream>`, no `<fstream>`, no `<cstdio>`. The exercise is about seeing the
kernel boundary clearly: which call does what, what each return value means,
and how bytes physically travel from disk to user memory.

## Files in this folder

| File | Role |
|---|---|
| `file_syscalls.cpp` | The program — seeds `input.txt` if missing, copies it to `output.txt`, prints the byte count |
| `input.txt` | Sample input (auto-created on first run with `O_CREAT \| O_EXCL`) |
| `output.txt` | The copied output (overwritten every run with `O_TRUNC`) |

## Build & Run

```bash
g++ -std=c++17 -O2 -Wall -Wextra file_syscalls.cpp -o file_syscalls
./file_syscalls
```

What I saw on my machine:

```
Copy finished. Bytes written: 160
```

(The byte count depends on the contents of `input.txt`. The seed payload in
`file_syscalls.cpp` produces 160 bytes.)

Quick verification:

```bash
$ wc -c input.txt output.txt
     160 input.txt
     160 output.txt
$ md5 input.txt output.txt        # macOS — use md5sum on Linux
MD5 (input.txt)  = 066489f812ff2b3501c4b52a783fcc77
MD5 (output.txt) = 066489f812ff2b3501c4b52a783fcc77
```

Identical hashes prove the copy is byte-exact.

---

## Design Decisions

A few choices in the implementation worth calling out:

- **16 KB buffer.** The buffer determines how often we cross the user/kernel
  boundary. 16 KB is large enough that a few-hundred-KB file copies in a
  handful of `read()`/`write()` pairs, and small enough to live in L1/L2.
  Doubling it past this point doesn't help — at that scale the bottleneck
  shifts from syscall entry cost to the actual `memcpy`.
- **`write_all()` short-write loop.** `write()` is allowed to return fewer
  bytes than requested. The wrapper re-issues the call until the entire
  buffer has been drained, and treats `EINTR` (interrupted by a signal) as
  retryable rather than fatal.
- **`posix_fadvise(POSIX_FADV_SEQUENTIAL)`.** A free hint to the kernel
  that we'll read this file front-to-back, which encourages more aggressive
  read-ahead on Linux. macOS doesn't define the constant, so the call is
  guarded with `#ifdef`.
- **No standard-library I/O at all.** Even error reporting goes through
  `write()` on `STDERR_FILENO`. A tiny `uint_to_ascii` helper turns the
  byte count into characters so the success message can avoid `printf` /
  `std::cout`.
- **`O_CREAT | O_EXCL` for seeding.** The first run creates `input.txt`
  atomically; if it already exists we leave it alone. This makes the
  program idempotent — re-running never destroys user-edited input.

---

## Concept Walkthrough

### 1. File descriptors

A file descriptor (FD) is just a small non-negative integer. Inside the
kernel it indexes into the calling process's FD table; each entry there
points to a kernel-side *open file description* that holds the current
read/write offset, the open flags, and a reference to the underlying inode.

Three FDs are wired up at process start:

- `0` — stdin
- `1` — stdout
- `2` — stderr

`open()` always returns the lowest free integer, which is why if you close
FD `0` and then `open()` a file, that file becomes FD `0`. Forgetting to
`close()` an FD leaks the underlying open-file description and pins the
inode in the kernel until process exit.

### 2. `dtruss` on macOS / `strace` on Linux

The cleanest way to confirm the program does only the syscalls you intended
is to trace it. On macOS:

```bash
sudo dtruss -t open,read,write,close ./file_syscalls 2>&1
```

On Linux:

```bash
strace -e trace=openat,read,write,close ./file_syscalls
```

The expected pattern when `input.txt` already exists:

```
openat(AT_FDCWD, "input.txt", O_RDONLY)                 = 3
openat(AT_FDCWD, "output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 4
read(3, "Lab 1 - raw POSIX...", 16384)                  = 160
write(4, "Lab 1 - raw POSIX...", 160)                   = 160
read(3, "", 16384)                                       = 0
close(3)                                                  = 0
close(4)                                                  = 0
```

The `read(... ) = 0` line is the EOF signal — not an error, just an empty
result that tells the loop to stop.

### 3. Inodes

Every file on a UNIX-like filesystem is identified internally by an
**inode number**, not by its name. The inode is a fixed-size on-disk
record holding:

- file type and mode bits
- owner UID / group GID
- size in bytes
- link count (how many directory entries point to this inode)
- access / modify / change timestamps
- pointers to the actual data blocks (direct + indirect, or extents)

The filename lives in the parent directory entry, which is itself a file
whose contents are name → inode mappings. That's why `ln input.txt
input2.txt` creates a second name pointing at the same inode — and why
deleting one of the names doesn't free the data until the link count
drops to zero.

### 4. The journey of a `read()`

1. Program calls `read(fd, buf, n)`. CPU traps into kernel mode.
2. Kernel resolves the FD → open file description → inode and current
   offset.
3. Kernel asks the page cache: do I already hold the relevant file pages?
4. **Cache hit:** kernel `memcpy`s straight from page-cache pages into the
   user buffer.
5. **Cache miss:** kernel queues a block I/O request, the storage driver
   programs a DMA transfer, the disk fills page-cache pages directly via
   the bus, an interrupt notifies the kernel, then the kernel copies from
   those pages into the user buffer.
6. Kernel advances the FD's offset and returns the byte count.
7. Return value `0` means EOF. A return smaller than `n` is normal (pipes,
   sockets, signals) and is NOT an error.

### 5. Pages and the page cache

The kernel manages physical RAM in **4 KB pages** (the typical x86_64 /
aarch64 page size; huge pages exist for large workloads). When file data
is read it lands in page-cache pages keyed by `(inode, offset)`. The next
read of the same range, by this process or any other, is then just a
`memcpy` from those pages — no disk needed. This is exactly why a query
that runs slowly the first time often runs much faster the second time.

### 6. Device drivers

User code never touches the disk directly. The path looks like:

```
program -> syscall -> VFS -> filesystem (apfs / ext4 / xfs)
        -> block layer -> driver (NVMe / AHCI / virtio-blk) -> hardware
```

Each layer has a stable interface to the layer above. The driver is the
only piece that has to know the hardware-specific command set, which is
why the same `read()` works whether the underlying device is an SSD, a
spinning disk, or a virtual block device.

### 7. DMA — Direct Memory Access

Older systems did "programmed I/O" — the CPU would read each byte from a
device register and store it to RAM in a tight loop. DMA flips this: the
storage controller is told *where* in RAM to put the data and *how much*
to transfer, and it then moves the data over the system bus by itself
while the CPU does other work. The CPU is involved only at the start
(setting up the DMA descriptor) and the end (handling the completion
interrupt). For large reads this makes the data-copy phase essentially
free in CPU terms.

### 8. Page-cache eviction

The page cache grows until something else needs the RAM. When it does, the
kernel uses an Active/Inactive two-list LRU approximation:

- **Inactive + clean** pages can be dropped instantly — they match disk.
- **Inactive + dirty** pages must be written back to disk before being
  evicted; this is what the writeback / flush machinery does.

This is why "first run slow, second run fast, then slow again after
something else ran" is such a common pattern: the unrelated workload
between your runs evicted the pages you cared about.

---

## Quick Verification Checklist

```bash
# byte counts must match
wc -c input.txt output.txt

# hashes must match
md5sum input.txt output.txt   # Linux
md5    input.txt output.txt   # macOS

# syscall summary table — verifies no unexpected calls
sudo dtruss -c ./file_syscalls    # macOS
strace -c ./file_syscalls         # Linux
```

---

## Summary

- POSIX I/O (`open` / `read` / `write` / `close`) is the thinnest portable
  layer above the kernel. Every higher-level I/O API in C and C++
  eventually calls into these.
- Short reads and short writes are normal — always loop.
- The page cache is the single biggest reason file I/O feels fast: a hit
  is a `memcpy`, a miss is a disk round-trip.
- DMA keeps the CPU out of the data-copy path between disk and RAM.
- `strace` (or `dtruss` on macOS) is the fastest way to see what your
  program is actually asking the kernel to do.