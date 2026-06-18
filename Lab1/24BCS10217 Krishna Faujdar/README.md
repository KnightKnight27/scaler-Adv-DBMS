# Lab 1: Raw File I/O with System Calls

**Role Number:** `24BCS10217`
**Name:** `Krishna Faujdar`

## Program Overview

- Source file: `file_syscalls.cpp`
- Input file: `input.txt`
- Output file: `output.txt`
- Behavior:
  - Creates `input.txt` with sample content if it does not already exist
  - Opens `input.txt` for reading
  - Opens/creates `output.txt` for writing
  - Copies all data from input to output using `read()` + `write()` in a loop
  - Closes all file descriptors before exit

## Build and Run

```bash
g++ -std=c++17 file_syscalls.cpp -o file_syscalls
./file_syscalls
```

Expected output:

```
Done. Copied input.txt -> output.txt using raw system calls.
```

---

## Learning Notes

### 1) File Descriptors

- A file descriptor (FD) is a small non-negative integer the kernel returns to represent an open file for a process.
- Standard descriptors: `0` = stdin, `1` = stdout, `2` = stderr.
- `open()` allocates and returns an FD from the lowest available slot in the process's FD table.
- `read(fd, ...)`, `write(fd, ...)`, and `close(fd)` all operate on that FD.
- FDs are per-process; the kernel maps them to open-file objects that track position, flags, and the underlying inode.

### 2) `strace`

- `strace` (Linux) intercepts and logs every system call and signal a process makes.
- Useful to verify that the program calls exactly the expected kernel interfaces.

```bash
strace -o trace.txt ./file_syscalls
```

- Typical entries visible in the trace:
  - `openat(AT_FDCWD, "input.txt", O_RDONLY)`
  - `read(3, "Hello...", 4096)`
  - `write(4, "Hello...", ...)`
  - `close(3)`, `close(4)`

*macOS alternative:* `dtruss` (requires disabling SIP or running as root).

### 3) Inodes

- An inode is the kernel's on-disk metadata record for a file.
- It stores: file type, permissions, owner/group, size, timestamps, and pointers to data blocks/extents.
- Inodes do **not** store the filename — directories hold the name-to-inode mapping.
- During `open("input.txt", ...)`, the kernel walks the directory tree, resolves the path to an inode number, verifies permissions, then creates an open-file entry and returns an FD.

### 4) Journey of Opening and Reading a File

1. `open("input.txt", O_RDONLY)` is called — CPU switches to kernel mode via syscall.
2. Kernel performs path resolution in the VFS layer, walking each directory component.
3. Kernel locates the inode and checks read permission against effective UID/GID.
4. Kernel allocates an open-file entry and an FD, returns the FD to user space.
5. `read(fd, buf, 4096)` is called — kernel checks if needed pages are in the page cache.
6. On a cache hit: data is copied straight from kernel page cache to the user buffer.
7. On a cache miss: kernel issues a block I/O request; driver (NVMe/SATA) handles DMA transfer into a page cache page; then copies to user buffer.
8. `read()` returns the byte count (`0` = EOF).
9. `close(fd)` releases the FD and decrements the open-file reference count.

---

## Additional Concepts

### 5) Pages

- Physical memory is managed in fixed-size chunks called pages (typically 4 KB on x86-64).
- The kernel keeps a **page cache**: recently accessed file data is held in RAM pages so future reads skip disk entirely.
- A `read()` is fast when the relevant pages are cached; slower when a page fault triggers disk I/O.

### 6) Device Drivers

- Drivers are kernel modules that control specific hardware.
- For storage, the stack is: VFS → filesystem → block layer → storage driver (NVMe, SATA, USB mass storage) → hardware.
- User programs never address hardware directly; they issue syscalls and the kernel dispatches to the right driver.

### 7) DMA (Direct Memory Access)

- DMA lets a device transfer data to/from RAM autonomously, without the CPU copying each byte.
- For disk reads, the storage controller uses DMA to write fetched sectors straight into a kernel page cache frame.
- CPU involvement is limited to setting up the DMA descriptor and handling the completion interrupt.
- DMA significantly reduces CPU overhead and improves I/O throughput.

### 8) Page Cache Eviction

- The page cache is bounded by available RAM.
- Under memory pressure the kernel evicts pages using an LRU-like policy (Active/Inactive lists on Linux).
- Clean pages (not modified since read) are simply dropped.
- Dirty pages (written but not yet synced to disk) must be flushed via `writeback` before they can be freed.
- Eviction explains why the same `read()` can be fast on the first run (pages warm) and slower after other workloads evict those pages.

---

## Summary

- Raw system calls (`open`, `read`, `write`, `close`) bypass the C standard library and talk to the kernel directly, giving full control over I/O behavior.
- File descriptors are lightweight process-level handles; inodes are the filesystem metadata objects behind filenames.
- The read path goes: user buffer ← page cache ← (on miss) storage driver via DMA.
- `strace` is the go-to tool to trace and verify which syscalls a program actually makes.
