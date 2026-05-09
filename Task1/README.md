# Task 1: Raw File I/O with System Calls

**Student Name:** Tuhin Samanta  
**Roll Number:** 24BCS10266

This assignment demonstrates file operations in C++ using raw POSIX system calls (`open`, `read`, `write`, `close`) instead of high-level file libraries.

---

## Program Overview

- Source file: `file_syscalls.cpp`
- Input file: `input.txt`
- Output file: `output.txt`

### Behavior:

- Creates `input.txt` (if not already present) with initial data.
- Opens the input file for reading.
- Opens/creates the output file for writing.
- Copies data from input to output using a 4KB buffer with `read()` + `write()`.
- Closes all file descriptors.

### Robustness Features:
- **`safe_write`**: Handles partial writes and signal interruptions (`EINTR`) to ensure data integrity.
- **Error Handling**: Uses `perror` and `errno` to provide descriptive error messages if a system call fails.

---

## Build and Run

```bash
g++ -std=c++17 file_syscalls.cpp -o task1
./task1
```

Expected output:

```text
Done. Copied input.txt to output.txt using system calls.
```

---

## Learning Notes

### 1) File Descriptors

- A file descriptor (FD) is a small integer returned by the kernel to represent an open file for a process.
- Standard descriptors:
  - `0`: stdin
  - `1`: stdout
  - `2`: stderr
- `open()` returns an FD.
- `read(fd, ...)`, `write(fd, ...)`, and `close(fd)` use that FD.
- FDs are process-level handles that point to kernel-managed open-file objects.

---

### 2) `strace`

- `strace` (Linux) traces system calls and signals for a process.
- It is useful to verify exactly what the program asks the kernel to do.

Example:

```bash
strace -o trace.txt ./task1
```

Typical calls visible in trace:
- `openat(...)`
- `read(...)`
- `write(...)`
- `close(...)`

---

### 3) Inodes in the Kernel

- An inode is the kernel metadata structure for a file.
- It stores:
  - file type and permissions
  - owner/group
  - file size
  - timestamps
  - disk block pointers / extent metadata
- It does not store the filename itself.
- Directories map filenames to inode numbers.
- During `open("input.txt", ...)`, the kernel resolves path components and reaches an inode before returning an FD.

---

### 4) Journey of Opening/Reading a File

1. Program calls `open("input.txt", O_RDONLY)`.
2. CPU transitions from user mode to kernel mode.
3. Kernel performs path lookup in directory structures.
4. Kernel finds inode and checks permissions.
5. Kernel creates an open-file entry and returns an FD.
6. Program calls `read(fd, buffer, n)`.
7. Kernel serves data from page cache (or fetches from storage if needed).
8. Data is copied into user buffer.
9. `read()` returns bytes read (`0` means EOF).
10. Program calls `close(fd)` and kernel releases references.

---

## Additional Concepts

### 5) Pages

- Memory is managed in fixed-size blocks called pages (commonly 4 KB).
- File data is cached by the kernel in page cache pages.
- A `read()` often succeeds quickly when required pages are already cached.

---

### 6) Drivers

- Device drivers are kernel components that control hardware.
- For storage I/O, filesystem and block-layer code eventually interact with storage drivers (NVMe/SATA/USB) to fetch or persist data.
- The program does not talk to hardware directly; it talks to kernel APIs, and drivers do the device-specific work.

---

### 7) DMA (Direct Memory Access)

- DMA allows devices to transfer data to/from RAM without the CPU copying each byte manually.
- In disk I/O paths, DMA reduces CPU overhead and improves throughput.
- CPU still coordinates setup/completion, but data movement is hardware-assisted.

---

### 8) Eviction

- The page cache has limited memory.
- When memory pressure rises, the kernel evicts less useful pages (often based on recency/usage heuristics).
- Clean pages can be dropped; dirty pages must be written back before eviction.
- This affects performance: repeated reads are faster if pages are still cached, slower if evicted and reloaded.

---

## My Understanding (Summary)

- System calls provide direct, explicit control over file I/O.
- File descriptors are the process-facing handles; inodes are the filesystem metadata objects behind pathnames.
- The `read` path often goes through page cache first, then drivers and hardware if cache misses.
- DMA and page cache are major performance helpers.
- Eviction explains why I/O latency can vary across repeated runs.
