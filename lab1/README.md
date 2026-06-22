# Lab 1: C++ File I/O Traced with Strace

## 1. Overview
This lab demonstrates the low-level system call journey of a standard C++ file I/O application. By using direct POSIX system calls (`open`, `write`, `read`, `close`) instead of high-level C++ stream abstractions, we reveal the exact boundary transitions between user-space and kernel-space, traced using the `strace` utility on Linux.

---

## 2. System Call Journey: Inode → VFS → Page Cache → Disk

When our C++ program performs file operations, the data traverses multiple layers of the Operating System kernel:

```text
  +-------------------------------------------------------------+
  |                   User Space Application                    |
  |                (lab1/main.cpp compiled binary)              |
  +-------------------------------------------------------------+
                                |
                        [ System Call ]  (e.g., openat, write, read)
                                v
  +-------------------------------------------------------------+
  |              Virtual File System (VFS) Layer                |
  |  - Abstract interface for all filesystems (ext4, XFS, etc.)  |
  |  - Inode resolution (path lookup -> dentry -> inode)        |
  +-------------------------------------------------------------+
                                |
                                v
  +-------------------------------------------------------------+
  |                     Page Cache (Memory)                     |
  |  - Writes: Data is copied to page frames, marked "dirty"    |
  |  - Reads: Page cache check. Hit -> return. Miss -> load     |
  +-------------------------------------------------------------+
                                |
                    [ Flusher threads (pdflush/dirty_writeback) ]
                                v
  +-------------------------------------------------------------+
  |                 Generic Block I/O Layer                     |
  |  - Organizes blocks, performs I/O scheduling (MQ-Deadlock)  |
  |  - Merges contiguous sectors to optimize disk writes        |
  +-------------------------------------------------------------+
                                |
                                v
  +-------------------------------------------------------------+
  |                Device Driver & Physical Disk                |
  |  - Write payload to NVMe SSD block or physical HDD sector   |
  +-------------------------------------------------------------+
```

### Detailed Layer Analysis

1. **Application Layer:** The program invokes system APIs. The CPU transitions from User Mode to Kernel Mode via a software interrupt/instruction (e.g., `sysenter` or `syscall` on x86_64).
2. **Virtual File System (VFS):**
   - **Path Resolution:** The kernel resolves the filename `"trace_demo.txt"`. It traverses directory entries (`dentries`) starting from the current working directory to find the file's **Inode**.
   - **Inode:** The data structure representing the file's metadata (permissions, owner, size, layout map of data blocks).
3. **Page Cache:**
   - **Write Path:** The kernel copies the data buffer from the user-space memory to page frames allocated in the system's physical RAM (page cache). The page is marked as **dirty**. The `write()` system call returns *immediately* after the copy to RAM, before the data touches physical storage.
   - **Flush Path:** The dirty pages are flushed to disk asynchronously by background kernel threads (e.g., `writeback` daemon) or explicitly when `fsync()` is called.
4. **Block I/O Layer:**
   - Converts filesystem pages (e.g., 4 KB chunks) into logical block addressing (LBA) requests.
   - The I/O scheduler reorders and merges requests to ensure sequential disk access.
5. **Physical Device Layer:**
   - The hardware device controller writes the blocks to physical storage cells (NAND flash or magnetic sectors).

---

## 3. Tracing with Strace (Linux)

### How to Compile and Run
Compile the program:
```bash
g++ -std=c++17 -Wall main.cpp -o app
```

Run and trace the program with `strace`:
```bash
strace -o strace_output.txt ./app
```

### Strace Output Breakdown

Looking inside `strace_output.txt`, you will see the following key system calls corresponding to our C++ functions:

#### 1. File Opening (`openat`)
```text
openat(AT_FDCWD, "trace_demo.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644) = 3
```
- **System Call:** `openat` is the modern version of `open`, resolving paths relative to directory file descriptors (here `AT_FDCWD` - active working directory).
- **Flags:** `O_WRONLY` (write-only), `O_CREAT` (create if missing), `O_TRUNC` (truncate file to 0 size).
- **Return Value:** `3`. This is the smallest unused file descriptor number (descriptors 0, 1, 2 are stdin, stdout, stderr).

#### 2. File Writing (`write`)
```text
write(3, "Hello, Advanced DBMS system call"..., 42) = 42
```
- **Arguments:** Writes to file descriptor `3`, passing the buffer and its size (42 bytes).
- **Return Value:** `42` (number of bytes successfully copied to the kernel's Page Cache).

#### 3. Console Output (`write`)
```text
write(1, "[C++] Bytes written: 42\n", 24) = 24
```
- **Stdout:** File descriptor `1` represents `stdout` (console). The C++ `cout` statements are translated to `write(1, ...)` system calls.

#### 4. Closing (`close`)
```text
close(3) = 0
```
- Closes the active file handle. The file descriptor `3` is now freed.

#### 5. Reopening & Reading (`openat` & `read`)
```text
openat(AT_FDCWD, "trace_demo.txt", O_RDONLY) = 3
read(3, "Hello, Advanced DBMS system call"..., 127) = 42
close(3) = 0
```
- The file is reopened read-only, returning file descriptor `3`.
- `read` requests 127 bytes from file descriptor `3` and returns `42` (the actual size of the file).
- The file descriptor is closed again.

---

## 4. Key Takeaways

1. **Buffering vs. Direct I/O:** Standard writes only touch RAM (Page Cache). To guarantee write persistence, programs must use `fsync()` or open the file with the `O_SYNC` flag.
2. **File Descriptors:** Act as integers mapped by the OS process control block to active system file descriptions.
3. **Mode Switching Cost:** Every system call involves context-switching overhead from user space to kernel space, which is why library buffering (like C++ `std::ofstream` or C `stdio`) is used to bundle small writes into larger, page-aligned disk writes.
