# syscall_io — Raw Linux System Call File I/O

**Anushka Jain | 24BCS10193**  
Advanced DBMS — Scaler | Lab 1

---

## What This Is

A C++ program that performs file I/O using **only raw Linux syscalls** — no `libc`, no `<cstdio>`, no `<fstream>`, no `printf`, no C++ runtime. The program is compiled with `-nostdlib -static`, which means the OS kernel is literally the only thing it talks to.

The entry point is `_start` (not `main`), because `main` is a convention of the C runtime, which we deliberately excluded.

---

## Quick Start

```bash
# Build
make

# Run
make run

# Or manually
g++ -nostdlib -static -o syscall_io syscall_io.cpp
./syscall_io
```

---

## Syscalls Used

| Syscall | Number (x86-64) | Signature | Role |
|---------|-----------------|-----------|------|
| `read`  | 0 | `ssize_t read(int fd, void *buf, size_t n)` | Read bytes from a file descriptor |
| `write` | 1 | `ssize_t write(int fd, const void *buf, size_t n)` | Write bytes to a file descriptor |
| `open`  | 2 | `int open(const char *path, int flags, mode_t mode)` | Open / create a file |
| `close` | 3 | `int close(int fd)` | Release a file descriptor |
| `exit`  | 60 | `void exit(int status)` | Terminate the process |

Numbers sourced from `arch/x86/entry/syscalls/syscall_64.tbl` in the Linux kernel tree.

---

## x86-64 Calling Convention for SYSCALL

The `SYSCALL` instruction uses registers rather than the stack:

| Register | Role |
|----------|------|
| `RAX` | Syscall number |
| `RDI` | Argument 1 |
| `RSI` | Argument 2 |
| `RDX` | Argument 3 |
| `R10` | Argument 4 |
| `R8`  | Argument 5 |
| `R9`  | Argument 6 |

Return value lands in `RAX`. Negative values indicate an error (errno negated).

---

## Write Path — Kernel Journey

```
User Program (Ring 3)
  |
  | 1. RAX=1, RDI=fd, RSI=buf_ptr, RDX=count
  | 2. Execute SYSCALL instruction
  |
  v
+--[ Privilege Switch: Ring 3 -> Ring 0 ]--+
|  CPU saves RIP -> RCX, RFLAGS -> R11     |
|  Jumps to MSR_LSTAR (kernel entry point) |
+------------------------------------------+
  |
  v
entry_SYSCALL_64
  | 3. Dispatch via sys_call_table[1] -> sys_write
  v
VFS Layer  (vfs_write)
  | 4. Validate fd in the process's open-file table
  | 5. Verify write permission
  | 6. Call filesystem-specific write_iter (e.g. ext4_file_write_iter)
  v
Page Cache
  | 7. copy_from_user(): bytes move from user buffer -> kernel page frame
  | 8. Page is marked DIRTY
  v
Writeback thread (kworker/pdflush)
  | 9. Periodically scans dirty pages
  | 10. Submits block I/O to the I/O scheduler
  v
I/O Scheduler (mq-deadline / BFQ)
  | 11. Merges and reorders requests for throughput/latency tradeoff
  v
Block Driver (NVMe / AHCI / virtio-blk)
  | 12. Programs controller via MMIO
  | 13. DMA: data moved from kernel buffer to storage
  v
Physical Storage (SSD / HDD)
  | 14. Data committed to NAND cells / magnetic platter
  | 15. Controller fires interrupt on completion
  v
Interrupt Handler
  | 16. Marks I/O request complete, clears DIRTY flag
  v
SYSRET (Ring 0 -> Ring 3)
  | 17. Bytes-written count returned in RAX
  v
User Program continues
```

---

## Read Path — Kernel Journey

```
User Program (Ring 3)
  |
  | 1. RAX=0, RDI=fd, RSI=buf_ptr, RDX=count
  | 2. SYSCALL
  v
entry_SYSCALL_64
  | 3. sys_call_table[0] -> sys_read
  v
VFS Layer (vfs_read)
  | 4. Validate fd and permissions
  | 5. Resolve inode, call filesystem read_iter
  v
Page Cache lookup
  |
  +--[HIT]----> 6a. Data is already in a kernel page frame
  |                  (common immediately after a write)
  |                  Skip disk entirely.
  |
  +--[MISS]---> 6b. Block I/O needed:
                    Scheduler -> Driver -> DMA -> page frame inserted into cache
  v
copy_to_user()
  | 7. Bytes moved from kernel page frame -> user buffer
  v
SYSRET (Ring 0 -> Ring 3)
  | 8. Bytes-read count in RAX
  v
User Program continues
```

---

## Key Concepts

### Page Cache
The kernel maintains an in-memory copy of recently accessed disk blocks. Writes go into the page cache first (marked dirty) and reach disk later via writeback threads. Reads check the cache before touching disk. In this demo, the read immediately after the write is a **guaranteed cache hit** — the data never left RAM.

### File Descriptors
A small non-negative integer the kernel uses to index into the per-process open-file table. The three fixed ones are `0` (stdin), `1` (stdout), `2` (stderr). Our file gets `3` since it's the first one opened.

### VFS (Virtual File System)
An abstraction layer that provides a uniform interface across ext4, XFS, Btrfs, tmpfs, etc. When `open()` is called, VFS resolves the path through the dentry cache to an inode, then delegates to the correct filesystem driver.

### Why `-nostdlib`?
`libc` functions like `fwrite`, `printf`, and `std::ofstream::write` are wrappers — internally they all invoke the same `write` syscall. Compiling without the standard library strips away those wrappers and forces every I/O operation to cross the kernel boundary explicitly.

### DBMS Relevance
Database engines (PostgreSQL, MySQL/InnoDB, SQLite) make deliberate choices at every layer of this stack:
- `O_DIRECT` bypasses the page cache for self-managed buffer pools
- `fsync()` / `fdatasync()` forces dirty pages to disk before acknowledging a commit (durability in ACID)
- WAL (Write-Ahead Logging) controls exactly which writes go where and in what order
Understanding raw syscall I/O is the foundation for understanding all of this.

---

## Problems Faced & How We Solved Them

### 1. Linker error: `undefined reference to 'main'`
**Problem:** The default C++ toolchain expects `main` as the entry point, provided by the C runtime (`crt0.o`/`crt1.o`). With `-nostdlib`, there is no runtime, so the linker complains.  
**Fix:** Define `extern "C" void _start()` as the entry point. The kernel's ELF loader jumps directly to `_start` without any runtime in between.

### 2. No `strlen`, `printf`, `itoa` — how to print anything?
**Problem:** Every standard utility function (`strlen`, `printf`, even `memset`) comes from `libc`, which we excluded.  
**Fix:** Wrote minimal inline replacements: `str_len` (walks until `'\0'`), `write_str` (calls `raw_write`), and `write_long` (builds a decimal string backwards in a local buffer).

### 3. Inline assembly clobber list
**Problem:** The `SYSCALL` instruction implicitly clobbers `RCX` (saves return address) and `R11` (saves RFLAGS). Not declaring these as clobbered lets the compiler store live values in those registers, causing silent corruption.  
**Fix:** Added `"rcx", "r11", "memory"` to the clobber list in every `__asm__ volatile` block.

### 4. `[[noreturn]]` on `_exit` wrapper
**Problem:** The compiler emitted a warning about a function that calls `SYS_EXIT` but has a non-void return type — it couldn't prove the path exits.  
**Fix:** Marked the wrapper `[[noreturn]]` and added `__builtin_unreachable()` after the inline asm so the compiler knows control never returns.

### 5. read buffer not zero-initialized
**Problem:** `read()` does not null-terminate the buffer. Passing the raw buffer to any string function after a partial read causes out-of-bounds access.  
**Fix:** Declared `char read_buf[512] = {}` (zero-initialized) and passed `511` as the count, guaranteeing a null terminator at position 511 regardless of how many bytes were read.

---

## Files

| File | Description |
|------|-------------|
| `syscall_io.cpp` | C++ source — raw syscalls only, entry point `_start` |
| `Makefile` | Build, run, and clean targets |
| `Assignment.md` | Full kernel journey documentation |
| `README.md` | This file |

---

## Requirements

- Linux x86-64
- `g++` (GCC 9+ recommended)
- No special libraries needed — that's the whole point
