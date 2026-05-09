#pragma once

/**
 * syscalls.h
 * Raw Linux syscall wrappers — no libc buffering, no FILE* abstraction.
 * Each function is a direct ring-0 kernel entry via the syscall instruction.
 *
 * Syscall numbers (x86-64 Linux):
 *   SYS_read    =  0
 *   SYS_write   =  1
 *   SYS_close   =  3
 *   SYS_lseek   =  8
 *   SYS_fsync   = 74
 *   SYS_openat  = 257
 */

#include <sys/types.h>   // off_t, ssize_t

// ── open ─────────────────────────────────────────────────────────────────────
/**
 * raw_open — syscall #257 SYS_openat
 *
 * Kernel path:
 *   1. Walks dentry cache to resolve path
 *   2. Allocates struct file with f_pos = 0
 *   3. Increments inode i_count
 *   4. Assigns lowest free index in per-process fd[] array
 *   5. Returns fd integer
 *
 * strace: openat(AT_FDCWD, "journey.txt", O_RDWR|O_CREAT|O_TRUNC, 0644) = 3
 */
long raw_open(const char *path, int flags, int mode);

// ── write ────────────────────────────────────────────────────────────────────
/**
 * raw_write — syscall #1 SYS_write
 *
 * Kernel path:
 *   1. fd → struct file → f_op->write_iter()
 *   2. copy_from_user() → page cache page
 *   3. Page marked DIRTY — no disk I/O yet
 *   4. f_pos advanced by n bytes
 *
 * strace: write(3, "Hello...", 29) = 29
 */
long raw_write(int fd, const void *buf, unsigned long n);

// ── read ─────────────────────────────────────────────────────────────────────
/**
 * raw_read — syscall #0 SYS_read
 *
 * Kernel path:
 *   HIT  → copy_to_user() from page cache; no I/O; ~100ns latency
 *   MISS → allocate page frame → submit bio → TASK_UNINTERRUPTIBLE
 *          → DMA completes → IRQ → wake → copy_to_user()
 *
 * strace: read(3, "Hello...", 255) = 62
 */
long raw_read(int fd, void *buf, unsigned long n);

// ── lseek ────────────────────────────────────────────────────────────────────
/**
 * raw_lseek — syscall #8 SYS_lseek
 *
 * Kernel path:
 *   Updates f_pos in struct file only.
 *   Zero page cache interaction. Zero I/O.
 *
 * strace: lseek(3, 0, SEEK_SET) = 0
 */
long raw_lseek(int fd, long offset, int whence);

// ── fsync ────────────────────────────────────────────────────────────────────
/**
 * raw_fsync — syscall #74 SYS_fsync
 *
 * Kernel path:
 *   1. Submits all dirty pages for this inode as bio structs
 *   2. I/O scheduler merges/reorders bios
 *   3. Block driver programs DMA descriptors
 *   4. DMA transfers page frames → device (CPU not involved)
 *   5. Device raises IRQ; ISR marks bios done
 *   6. Inode metadata (mtime, size) also flushed
 *   7. Blocks until complete → returns 0
 *
 * THIS is where DMA actually fires.
 * strace: fsync(3) = 0
 */
long raw_fsync(int fd);

// ── close ────────────────────────────────────────────────────────────────────
/**
 * raw_close — syscall #3 SYS_close
 *
 * Kernel path:
 *   1. Removes fd from per-process fd[] array
 *   2. Decrements struct file f_count
 *   3. If f_count == 0: struct file freed; inode i_count decremented
 *   4. If i_count == 0 AND i_nlink == 0: blocks deallocated
 *
 * strace: close(3) = 0
 */
long raw_close(int fd);
