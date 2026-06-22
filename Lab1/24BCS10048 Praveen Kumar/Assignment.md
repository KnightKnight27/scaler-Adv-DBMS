# warp si -- Raw System Call File I/O

**Course:** Advanced DBMS (Scaler)
**Author:** Praveen Kumar 24bcs10048
**Date:** 2026-05-04

---

## 1. Objective

Write a C++ program that performs file read and write operations using **only raw Linux system calls**. No standard library (`libc`), no `<cstdio>`, no `<fstream>`, no `printf` -- nothing but the `SYSCALL` instruction and the Linux kernel ABI.

This document traces the full journey every byte takes through the operating system stack.

---

## 2. Build and Run

```bash
# Compile (no C runtime, fully static)
g++ -nostdlib -static -o warp_si warp_si.cpp

# Run
./warp_si
```

The program creates a file called `warpsi_data.txt`, writes a message into it, reads the message back, and prints a step-by-step trace to the terminal.

---

## 3. System Calls Used

| Syscall | Number (x86-64) | Signature | Purpose |
|---------|-----------------|-----------|---------|
| `open`  | 2  | `int open(const char *path, int flags, mode_t mode)` | Open or create a file |
| `write` | 1  | `ssize_t write(int fd, const void *buf, size_t count)` | Write bytes to a file descriptor |
| `read`  | 0  | `ssize_t read(int fd, void *buf, size_t count)` | Read bytes from a file descriptor |
| `close` | 3  | `int close(int fd)` | Release a file descriptor |
| `exit`  | 60 | `void exit(int status)` | Terminate the process |

All system call numbers are sourced from `arch/x86/entry/syscalls/syscall_64.tbl` in the Linux kernel source tree.

---

## 4. The Journey of a Write Operation

Below is the end-to-end path a `write()` call takes, from the user program all the way to the physical disk.

```
User Program (Ring 3)
       |
       |  1. Load syscall number into RAX, arguments into RDI/RSI/RDX
       |  2. Execute SYSCALL instruction
       |
       v
+----- PRIVILEGE SWITCH (Ring 3 -> Ring 0) -----+
|  CPU saves RIP to RCX, RFLAGS to R11          |
|  Jumps to kernel entry via MSR_LSTAR          |
+------------------------------------------------+
       |
       v
Kernel Entry (entry_SYSCALL_64)
       |
       |  3. Dispatch via sys_call_table[RAX]
       |
       v
VFS Layer (vfs_write)
       |
       |  4. Validate file descriptor in process fd table
       |  5. Check access permissions
       |  6. Call filesystem-specific .write_iter method
       |
       v
Filesystem (e.g. ext4_file_write_iter)
       |
       |  7. Allocate or locate data blocks
       |  8. Update inode metadata (size, timestamps)
       |
       v
Page Cache (write-back)
       |
       |  9. copy_from_user(): move bytes from user buffer
       |     into a kernel page frame
       | 10. Mark the page as DIRTY
       |
       v
Writeback Thread (pdflush / kworker)
       |
       | 11. Periodically scans dirty pages
       | 12. Submits block I/O requests to the I/O scheduler
       |
       v
I/O Scheduler (e.g. mq-deadline, BFQ)
       |
       | 13. Reorders and merges I/O requests for efficiency
       |
       v
Block Device Driver (SCSI / NVMe / AHCI)
       |
       | 14. Programs the disk controller via MMIO or port I/O
       | 15. Initiates DMA transfer from kernel buffer to disk
       |
       v
Physical Storage (SSD / HDD)
       |
       | 16. Data is written to NAND flash cells / magnetic platter
       | 17. Disk controller sends interrupt on completion
       |
       v
Kernel Interrupt Handler
       |
       | 18. Marks the I/O request as complete
       | 19. Clears the DIRTY flag on the page
       |
       v
SYSCALL returns to user space (SYSRET instruction)
       |
       | 20. User program receives bytes-written count in RAX
       v
User Program continues
```

---

## 5. The Journey of a Read Operation

```
User Program (Ring 3)
       |
       |  1. Load syscall number (0) into RAX
       |  2. Execute SYSCALL instruction
       |
       v
+----- PRIVILEGE SWITCH (Ring 3 -> Ring 0) -----+
       |
       v
Kernel Entry (entry_SYSCALL_64)
       |
       |  3. Dispatch to sys_read via sys_call_table[0]
       |
       v
VFS Layer (vfs_read)
       |
       |  4. Validate fd and permissions
       |  5. Call filesystem .read_iter
       |
       v
Page Cache Lookup
       |
       +--[HIT]--> 6a. Data already in memory (common after a
       |                recent write). Skip disk I/O entirely.
       |
       +--[MISS]-> 6b. Submit a block I/O request:
                        I/O Scheduler -> Block Driver -> Disk
                        DMA transfers data into a new page frame.
                        Page is inserted into the page cache.
       |
       v
copy_to_user()
       |
       |  7. Kernel copies bytes from the page cache frame
       |     into the user-space buffer.
       |
       v
SYSRET (Ring 0 -> Ring 3)
       |
       |  8. User program receives byte count in RAX
       v
User Program continues
```

---

## 6. Key Concepts Illustrated

### 6.1 The SYSCALL Instruction

The `SYSCALL` instruction is the modern (x86-64) mechanism for transitioning from user mode to kernel mode. It is significantly faster than the older `INT 0x80` approach because it avoids the overhead of the Interrupt Descriptor Table lookup.

| Register | Role |
|----------|------|
| RAX | System call number |
| RDI | 1st argument |
| RSI | 2nd argument |
| RDX | 3rd argument |
| R10 | 4th argument |
| R8  | 5th argument |
| R9  | 6th argument |

### 6.2 File Descriptors

A file descriptor is a small non-negative integer that the kernel uses as an index into the per-process open-file table. Standard descriptors:

- `0` = stdin
- `1` = stdout
- `2` = stderr
- `3+` = files opened by the process

### 6.3 The Page Cache

The page cache is the kernel's in-memory copy of recently accessed disk blocks. It serves as a transparent read/write cache:

- **On write:** data goes into the page cache first (marked dirty), and is flushed to disk later.
- **On read:** if the requested block is already cached, the kernel returns it instantly without touching the disk.

This is why, in our demo, the read after write is extremely fast: the data never left RAM.

### 6.4 VFS (Virtual File System)

The VFS is an abstraction layer that lets the kernel support many different filesystems (ext4, XFS, Btrfs, etc.) through a single uniform interface. When our program calls `open()`, the VFS resolves the path and delegates to the correct filesystem driver.

---

## 7. Why No Libraries?

Using raw system calls demonstrates:

1. **What the OS actually does** when you call `fwrite()` or `std::ofstream::write()`. Those functions are wrappers; the real work is the `SYSCALL` instruction.
2. **The privilege boundary** between user space and kernel space. Every file I/O operation requires crossing this boundary.
3. **How the kernel manages storage** through layers: VFS, filesystem, page cache, I/O scheduler, and block drivers.

In an Advanced DBMS context, understanding this stack is critical because database engines (PostgreSQL, MySQL/InnoDB, SQLite) make deliberate choices about when and how to bypass or utilize each layer (e.g., `O_DIRECT` to skip the page cache, `fsync()` to force durability).

---

## 8. Sample Output

```
==============================================================
  warp si -- Raw System Call File I/O Journey
==============================================================

[PHASE 1] WRITE PATH
------------------------------------------------------------

  Step 1 : open("warpsi_data.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)
           Kernel path lookup via VFS --> dentry cache --> inode.
           O_CREAT: allocate new inode if file absent.
           O_TRUNC: discard existing contents.
           Returns lowest free fd in process table.
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: fd = 3  [OK]

  Step 2 : write(fd, payload, length)
           Kernel validates fd, checks user-space pointer.
           Calls ext4_file_write_iter (or similar).
           Data is copied into the PAGE CACHE (dirty page).
           Writeback threads will flush to disk asynchronously.
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: bytes_written = 130  [OK]

  Step 3 : close(fd)
           Kernel decrements struct file refcount.
           If refcount == 0, filesystem .release is called.
           fd slot freed for reuse.
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: success  [OK]

[PHASE 2] READ PATH
------------------------------------------------------------

  Step 1 : open("warpsi_data.txt", O_RDONLY)
           Kernel path lookup (likely dcache hit this time).
           O_RDONLY: no write permission requested.
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: fd = 3  [OK]

  Step 2 : read(fd, buffer, 256)
           Kernel checks fd, looks up page cache.
           PAGE CACHE HIT (data was just written).
           copy_to_user() transfers bytes to our buffer.
           No disk I/O needed (warm cache).
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: bytes_read = 130  [OK]

  Step 3 : close(fd)
           Executing SYSCALL (Ring3 -> Ring0) ...
           Result: closed  [OK]

[PHASE 3] VERIFICATION
------------------------------------------------------------

  Data read back from file:
  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
  Hello from warp si!
  This data was written using raw Linux syscalls.
  No libc. No fprintf. No fstream. Just registers and SYSCALL.
  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

  Bytes written : 130
  Bytes read    : 130
  Match         : YES

==============================================================
  Journey complete.  All I/O used raw syscalls only.
==============================================================
```

---

## 9. Files in This Submission

| File | Description |
|------|-------------|
| `warp_si.cpp` | The C++ source code (raw syscalls, no libraries) |
| `Makefile` | Build instructions |
| `Assignment.md` | This document: full journey documentation |
| `README.md` | Quick-start guide |

---

## 10. References

- Linux kernel source: `arch/x86/entry/syscalls/syscall_64.tbl`
- Linux kernel source: `include/uapi/asm-generic/fcntl.h`
- Kerrisk, M. *The Linux Programming Interface*, Ch. 4 (File I/O)
- Love, R. *Linux Kernel Development*, Ch. 13 (VFS), Ch. 16 (Page Cache)
