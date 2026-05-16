# Task 1: Raw File I/O with System Calls

This assignment demonstrates file operations in C++ using raw POSIX system calls (`open`, `read`, `write`, `close`) instead of high-level file libraries.

## Program Overview

- Source file: `rawcalls.cpp`
- Input file: `input.txt`
- Output file: `output.txt`
- Behavior:
  - Creates `input.txt` (if not already present)
  - Opens the input file for reading
  - Opens/creates the output file for writing
  - Copies data from input to output using `read()` + `write()`
  - Closes all file descriptors

## Build and Run

```bash
g++ -std=c++17 rawcalls.cpp -o rawcalls
./rawcalls
```

Expected output:

```text
Copied from input.txt to output.txt using system calls.
```

---

## Learning Notes

### 1) File Descriptors

- A file descriptor (FD) is a small integer the kernel returns to identify an open file for a process.
- Standard descriptors:
  - `0`: stdin
  - `1`: stdout
  - `2`: stderr
- `open()` gives back an FD.
- `read(fd, ...)`, `write(fd, ...)`, and `close(fd)` operate on that FD.
- FDs are per-process handles that map to kernel open-file entries.

### 2) `strace`

- `strace` (Linux) records system calls and signals made by a process.
- It helps confirm what the program asks the kernel to execute.
- Example:

```bash
strace -o trace.txt ./rawcalls
```

- Typical calls visible in trace:
  - `openat(...)`
  - `read(...)`
  - `write(...)`
  - `close(...)`

macOS note: `strace` is Linux-only; comparable tracing can be done with `dtruss` (permissions and security settings may apply).

### 3) Inodes in the Kernel

- An inode is the kernel metadata record for a file.
- It stores:
  - file type and permissions
  - owner/group
  - file size
  - timestamps
  - disk block pointers / extents
- The inode does not contain the filename.
- Directories map names to inode numbers.
- During `open("input.txt", ...)`, the kernel resolves the path to an inode before returning an FD.

### 4) Journey of Opening/Reading a File

1. Program calls `open("input.txt", O_RDONLY)`.
2. CPU switches from user mode to kernel mode.
3. Kernel performs path lookup in directory data.
4. Kernel locates the inode and checks permissions.
5. Kernel creates an open-file entry and returns an FD.
6. Program calls `read(fd, buffer, n)`.
7. Kernel serves data from page cache or fetches from storage.
8. Data is copied into the user buffer.
9. `read()` returns bytes read (`0` means EOF).
10. Program calls `close(fd)` and the kernel drops references.

---

## Additional Concepts

### 5) Pages

- Memory is managed in fixed-size blocks called pages (commonly 4 KB).
- File data is cached by the kernel in page cache pages.
- A `read()` is faster when the needed pages are already cached.

### 6) Drivers

- Device drivers are kernel components that control hardware.
- For storage I/O, filesystem and block-layer code eventually talk to device drivers (NVMe/SATA/USB) to read or write data.
- The program never touches hardware directly; it calls kernel APIs and drivers handle the device details.

### 7) DMA (Direct Memory Access)

- DMA lets devices move data to/from RAM without the CPU copying each byte.
- In disk I/O paths, DMA reduces CPU load and improves throughput.
- The CPU still coordinates setup and completion, but data movement is hardware-assisted.

### 8) Eviction

- The page cache is limited.
- Under memory pressure, the kernel evicts less useful pages (often by recency/usage).
- Clean pages can be dropped; dirty pages must be written back before eviction.
- Performance varies: repeated reads are faster if pages remain cached and slower after eviction.

---

## My Understanding (Summary)

- System calls provide direct, explicit control over file I/O.
- File descriptors are process-level handles; inodes are the filesystem metadata records behind names.
- The read path usually hits page cache first, then drivers and hardware on a cache miss.
- DMA and page cache are key performance helpers.
- Eviction explains why I/O latency can vary across runs.
