# syscall_io — Raw System Call File I/O

**Course:** Advanced DBMS (Scaler)  
**Author:** Anushka Jain | 24BCS10193  
**Date:** 2026-05-08

---

## 1. Objective

Write a C++ program that performs file read/write using **only raw Linux system calls**. No standard library, no `libc`, no `<cstdio>`, no `<fstream>`, no `printf`. Only the `SYSCALL` instruction and the Linux kernel ABI.

The program is compiled with `-nostdlib -static` and uses `_start` as the entry point instead of `main`.

---

## 2. Build and Run

```bash
# Compile (no C runtime, fully static)
g++ -nostdlib -static -o syscall_io syscall_io.cpp

# Run
./syscall_io
```

The program creates `syscall_io_data.txt`, writes a message into it, reads it back, and prints a step-by-step trace to stdout.

---

## 3. System Calls Used

| Syscall | Number (x86-64) | Signature | Purpose |
|---------|-----------------|-----------|---------|
| `read`  | 0 | `ssize_t read(int fd, void *buf, size_t count)` | Read bytes from fd |
| `write` | 1 | `ssize_t write(int fd, const void *buf, size_t count)` | Write bytes to fd |
| `open`  | 2 | `int open(const char *path, int flags, mode_t mode)` | Open / create a file |
| `close` | 3 | `int close(int fd)` | Release a file descriptor |
| `exit`  | 60 | `void exit(int status)` | Terminate the process |

Numbers from `arch/x86/entry/syscalls/syscall_64.tbl` in the Linux kernel source.

---

## 4. x86-64 Register ABI for SYSCALL

```
RAX  = syscall number
RDI  = argument 1
RSI  = argument 2
RDX  = argument 3
R10  = argument 4
R8   = argument 5
R9   = argument 6

Return value -> RAX
Negative RAX -> error (negated errno)
```

`SYSCALL` implicitly clobbers `RCX` (saves RIP) and `R11` (saves RFLAGS). These must be declared in the asm clobber list.

---

## 5. Write Path — End to End

```
User Program (Ring 3)
      |
      |  1. RAX = 1 (SYS_WRITE), RDI = fd, RSI = buf, RDX = count
      |  2. Execute SYSCALL instruction
      |
      v
+-----[ Privilege Switch: Ring 3 -> Ring 0 ]------+
|  CPU saves RIP into RCX                          |
|  CPU saves RFLAGS into R11                       |
|  Jumps to address stored in MSR_LSTAR            |
+-------------------------------------------------+
      |
      v
  entry_SYSCALL_64  (arch/x86/entry/entry_64.S)
      |
      |  3. Dispatch via sys_call_table[RAX=1] -> sys_write
      |
      v
  VFS Layer  (fs/read_write.c :: vfs_write)
      |
      |  4. Look up fd in current->files->fdt->fd[fd]
      |  5. Verify struct file has FMODE_WRITE
      |  6. Invoke file->f_op->write_iter
      |
      v
  Filesystem  (e.g. fs/ext4/file.c :: ext4_file_write_iter)
      |
      |  7. Determine target inode and block offsets
      |  8. Update inode: i_size, i_mtime, i_ctime
      |
      v
  Page Cache  (mm/filemap.c)
      |
      |  9. copy_from_user(): bytes moved from user buffer
      |     into a kernel page frame
      | 10. Page marked DIRTY (PG_dirty bit set)
      |
      v
  Writeback Thread  (kworker / mm/page-writeback.c)
      |
      | 11. Periodic scan of dirty pages
      | 12. Submits bio (block I/O) to the I/O scheduler
      |
      v
  I/O Scheduler  (mq-deadline / BFQ)
      |
      | 13. Reorders / merges adjacent requests
      | 14. Hands request to block device driver
      |
      v
  Block Driver  (NVMe / AHCI / virtio-blk)
      |
      | 15. Programs controller registers via MMIO
      | 16. DMA: data transferred from page frame to storage
      |
      v
  Physical Storage (SSD / HDD)
      |
      | 17. NAND flash cells / magnetic platter updated
      | 18. Controller fires IRQ on completion
      |
      v
  Interrupt Handler
      |
      | 19. bio marked complete
      | 20. PG_dirty cleared on the page
      |
      v
  SYSRET  (Ring 0 -> Ring 3)
      |
      | 21. Bytes-written count in RAX
      v
  User Program continues
```

---

## 6. Read Path — End to End

```
User Program (Ring 3)
      |
      |  1. RAX = 0 (SYS_READ), RDI = fd, RSI = buf, RDX = count
      |  2. Execute SYSCALL instruction
      |
      v
  entry_SYSCALL_64
      |
      |  3. sys_call_table[0] -> sys_read
      |
      v
  VFS Layer  (vfs_read)
      |
      |  4. Validate fd, check FMODE_READ
      |  5. file->f_op->read_iter
      |
      v
  Page Cache Lookup
      |
      +---[HIT]---> 6a. Page already in memory (guaranteed after a
      |                   recent write to the same file).
      |                   Zero disk I/O; skip to copy_to_user.
      |
      +---[MISS]--> 6b. Allocate a new page frame.
                        Submit read bio to I/O scheduler.
                        Block driver initiates DMA from disk.
                        Page frame inserted into page cache.
      |
      v
  copy_to_user()
      |
      |  7. Bytes copied from kernel page frame -> user buffer
      |
      v
  SYSRET  (Ring 0 -> Ring 3)
      |
      |  8. Bytes-read count in RAX
      v
  User Program continues
```

---

## 7. Key Concepts

### 7.1 The SYSCALL Instruction
`SYSCALL` is the modern (x86-64) mechanism for crossing the user/kernel privilege boundary. It is faster than the legacy `INT 0x80` because it avoids an IDT lookup; the kernel entry address is stored in `MSR_LSTAR` and loaded directly by the CPU.

### 7.2 File Descriptors
A file descriptor is a non-negative integer indexing into the per-process open-file table (`struct files_struct`). Entries 0, 1, 2 are pre-assigned (stdin, stdout, stderr); the next `open()` call gets the lowest free slot, typically 3.

### 7.3 The Page Cache
The page cache holds in-memory copies of disk blocks. It is transparent: `write()` copies data in (marking the page dirty), and `read()` retrieves from cache if available. In this program, the read immediately follows the write, so it is always a cache hit — no disk I/O occurs on the read path.

### 7.4 VFS (Virtual File System)
VFS is the kernel abstraction that decouples syscall interfaces from filesystem implementations. `open()`, `read()`, `write()`, and `close()` always go through VFS; VFS dispatches to the actual driver (ext4, XFS, Btrfs, tmpfs, etc.) through function pointers in `struct file_operations`.

### 7.5 Writeback and Durability
By default, `write()` is asynchronous with respect to disk: data lands in the page cache and the call returns. For durability (ACID "D"), a database must call `fsync()` or `fdatasync()` to force dirty pages to storage before confirming a transaction. This is why PostgreSQL and MySQL write WAL records and call `fsync` on each commit by default.

---

## 8. Why No Standard Library?

1. **Exposes the real abstraction boundary.** `fwrite`, `std::ofstream`, `printf` are convenience wrappers. The actual work — the `SYSCALL` instruction — is what this program does directly.
2. **Shows the privilege switch.** Every file I/O requires Ring 3 → Ring 0 → Ring 3. No amount of user-space code can bypass this.
3. **DBMS context.** PostgreSQL's direct I/O mode (`O_DIRECT`), SQLite's WAL, and InnoDB's doublewrite buffer all make decisions at the syscall and VFS level. Knowing this stack is essential for understanding those design choices.

---

## 9. Sample Output

```
================================================================
  syscall_io  --  Raw Linux System Call File I/O Demo
  Author : Anushka Jain | 24BCS10193
  No libc. No stdlib. No printf. Only SYSCALL.
================================================================

[PHASE 1 : WRITE PATH]
----------------------------------------------------------------
  Step 1 : open("syscall_io_data.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)
           VFS resolves the path through dentry cache -> inode.
           O_CREAT  : allocate a fresh inode if the file is absent.
           O_TRUNC  : discard any existing content.
           Kernel returns the lowest free slot in the process fd table.
           Issuing SYSCALL 2 (Ring 3 -> Ring 0) ...
           => fd = 3  [OK]
  Step 2 : write(fd, payload, len)
           Kernel validates fd and checks the user-space pointer.
           Calls the filesystem's write_iter (e.g. ext4_file_write_iter).
           copy_from_user() moves bytes into a kernel PAGE CACHE frame.
           Page is marked DIRTY; writeback thread will flush later.
           Issuing SYSCALL 1 (Ring 3 -> Ring 0) ...
           => bytes_written = 222  [OK]
  Step 3 : close(write_fd)
           Decrements struct file refcount.
           If refcount hits zero, the filesystem .release hook fires.
           Fd slot is freed for reuse.
           Issuing SYSCALL 3 (Ring 3 -> Ring 0) ...
           => closed  [OK]

[PHASE 2 : READ PATH]
----------------------------------------------------------------
  Step 1 : open("syscall_io_data.txt", O_RDONLY)
           VFS path lookup (likely a dentry-cache HIT this time).
           O_RDONLY: no write permission needed.
           Issuing SYSCALL 2 (Ring 3 -> Ring 0) ...
           => fd = 3  [OK]
  Step 2 : read(fd, buffer, 512)
           Kernel validates fd, checks permissions.
           Looks up the page cache for this inode + offset.
           PAGE CACHE HIT: data is still in RAM from the write above.
           copy_to_user() transfers bytes directly to our buffer.
           Zero disk I/O required (warm cache).
           Issuing SYSCALL 0 (Ring 3 -> Ring 0) ...
           => bytes_read  = 222  [OK]
  Step 3 : close(read_fd)
           Issuing SYSCALL 3 (Ring 3 -> Ring 0) ...
           => closed  [OK]

[PHASE 3 : VERIFICATION]
----------------------------------------------------------------
  Data read back from disk:
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
  Hello from syscall_io!
  Written by Anushka Jain (24BCS10193) using raw Linux syscalls.
  Path: user space -> SYSCALL -> VFS -> page cache -> disk.
  No libc. No fstream. No fprintf. Just RAX and the kernel.
  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Bytes written : 222
  Bytes read    : 222
  Match         : YES

================================================================
  Journey complete.  All I/O used raw Linux syscalls only.
================================================================
```

---

## 10. Files

| File | Description |
|------|-------------|
| `syscall_io.cpp` | C++ source (raw syscalls, entry point `_start`) |
| `Makefile` | Build / run / clean |
| `Assignment.md` | This document |
| `README.md` | Quick-start guide |

---

## 11. References

- Linux kernel: `arch/x86/entry/syscalls/syscall_64.tbl`
- Linux kernel: `include/uapi/asm-generic/fcntl.h`
- Kerrisk, M. *The Linux Programming Interface*, Ch. 4 (File I/O)
- Love, R. *Linux Kernel Development*, Ch. 13 (VFS), Ch. 16 (Page Cache)
