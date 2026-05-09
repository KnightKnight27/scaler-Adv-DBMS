# Task 1: Raw File I/O with System Calls

**Student Name:** Tuhin Samanta  
**Roll Number:** 24BCS10266

This assignment demonstrates file operations in C++ using raw POSIX system calls (`open`, `read`, `write`, `close`) instead of high-level file libraries.

---

## Program Overview

- Source file: `file_syscalls.cpp`
- Build system: `Makefile`
- Input file: `input.txt` (auto-created with seed data on first run)
- Output file: `output.txt` (truncated on each run)

### Behavior (3-Phase Flow):

1. **Phase 1** — Creates `input.txt` (if not already present) with seed payload using `O_CREAT | O_EXCL` to avoid overwriting. Checks `errno == EEXIST` to distinguish "already exists" from actual errors.
2. **Phase 2** — Opens source for reading (`O_RDONLY`) and destination for writing (`O_CREAT | O_TRUNC`).
3. **Phase 3** — Copies data via a 4KB buffer loop: `read()` from source → `safe_write()` to destination, until `read()` returns 0 (EOF).
4. **Finalize** — Closes all FDs and writes completion message via raw `write(STDOUT_FILENO, ...)`.

### Robustness Features:
- **`safe_write`**: Loops over `write()` to handle partial writes and resumes on `EINTR` (signal interruption), ensuring all bytes are written.
- **`log_info`**: Uses raw `write(STDOUT_FILENO, ...)` instead of `printf`, demonstrating direct syscall usage for output.
- **`O_EXCL` with `EEXIST` check**: Atomically fails if the target already exists during creation, preventing accidental data loss.
- **Error Handling**: Every syscall is checked; on failure, FDs are closed and `perror` reports descriptive messages.
- **Permission Mode**: Files are created with `0664` (owner + group: rw-rw-r--), ensuring the program has write access while keeping files readable by collaborators.

---

## Build and Run

### Using Make (recommended)
```bash
make        # builds: g++ -std=c++17 -Wall -Wextra file_syscalls.cpp -o task1
./task1    # runs the program
make clean # removes: task1 binary + input.txt + output.txt
```

### Manual compilation
```bash
g++ -std=c++17 -Wall -Wextra file_syscalls.cpp -o task1
./task1
```

Expected output:

```text
$ ./task1
Done. Copied input.txt to output.txt using system calls.

$ cat output.txt
Initial data for the system call lab.
Generated using low-level I/O.
```

---

## Verification with `strace`

`strace` traces every system call made by the program, confirming the kernel interaction:

```bash
strace -o trace.txt ./task1
cat trace.txt
```

Key syscalls visible in the trace:

| Syscall | Purpose |
|---------|---------|
| `openat(..., "input.txt", O_RDONLY)` | Phase 2: open source for reading |
| `openat(..., "output.txt", O_WRONLY\|O_CREAT\|O_TRUNC, 0664)` | Phase 2: open/create destination |
| `read(3, "Initial data...", 4096)` | Phase 3: read from source |
| `write(4, "Initial data...", 47)` | Phase 3: write to destination |
| `close(3)` / `close(4)` | Finalize: release FDs |
| `write(1, "Done...", 51)` | Output completion message to stdout |

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

### 2) Inodes in the Kernel

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

### 4) File Permissions (umask)

- Files created with `open(..., mode)` apply the mode and then the process `umask` subtracts bits.
- `0664` means: owner + group get read+write; others get read.
- After umask (e.g., `0022`), effective permission is `0642` (owner: rw-, group: r--, others: r--).
- Check with `ls -l input.txt` to see the actual permissions on disk.

---

### 5) Journey of Opening/Reading a File

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

### 7) Pages

- Memory is managed in fixed-size blocks called pages (commonly 4 KB).
- File data is cached by the kernel in page cache pages.
- A `read()` often succeeds quickly when required pages are already cached.

---

### 8) Drivers

- Device drivers are kernel components that control hardware.
- For storage I/O, filesystem and block-layer code eventually interact with storage drivers (NVMe/SATA/USB) to fetch or persist data.
- The program does not talk to hardware directly; it talks to kernel APIs, and drivers do the device-specific work.

---

### 9) DMA (Direct Memory Access)

- DMA allows devices to transfer data to/from RAM without the CPU copying each byte manually.
- In disk I/O paths, DMA reduces CPU overhead and improves throughput.
- CPU still coordinates setup/completion, but data movement is hardware-assisted.

---

### 10) Eviction

- The page cache has limited memory.
- When memory pressure rises, the kernel evicts less useful pages (often based on recency/usage heuristics).
- Clean pages can be dropped; dirty pages must be written back before eviction.
- This affects performance: repeated reads are faster if pages are still cached, slower if evicted and reloaded.

---

## My Understanding (Summary)

- System calls (`open`, `read`, `write`, `close`) provide direct kernel-level control over file I/O without buffering or stdio overhead.
- File descriptors are process-level handles; inodes are the filesystem metadata objects that pathnames resolve into.
- A `read()` goes through the page cache first (fast), then drivers and hardware only on cache misses.
- DMA offloads data movement to hardware; page cache reduces disk traffic; eviction explains why I/O latency fluctuates across runs.
- `safe_write` demonstrates defensive coding: retry on EINTR, loop to handle partial writes.

