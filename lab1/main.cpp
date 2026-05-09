/**
 * Advanced DBMS Assignment
 * Topic: File I/O via Raw Linux System Calls
 *
 * Journey: open → write → write → lseek → read → lseek → fsync → close
 *
 * Build:
 *   g++ -std=c++20 -O0 -o kernel_io_journey main.cpp syscalls.cpp kprint.cpp
 *
 * Trace:
 *   strace -e trace=openat,read,write,close,lseek,fsync \
 *          -o strace_output.txt ./kernel_io_journey
 *
 * Inspect inode:
 *   stat journey.txt
 *   ls -li journey.txt
 */

#include "syscalls.h"
#include "kprint.h"
#include <sys/syscall.h>   // SYS_write (for inline stdout in read step)
#include <fcntl.h>         // O_RDWR, O_CREAT, O_TRUNC, SEEK_SET, SEEK_END
#include <sys/stat.h>      // S_IRUSR, S_IWUSR, S_IRGRP, S_IROTH
#include <unistd.h>        // syscall()

int main() {
    const char *filename = "journey.txt";

    // ── STEP 1: OPEN ─────────────────────────────────────────────────────────
    // Kernel: dentry walk → allocate struct file → increment inode i_count
    //         → assign lowest free fd[] index → return fd integer
    // fd=3 expected (0=stdin, 1=stdout, 2=stderr already taken)
    kprint("[1] Opening file...\n");
    const long fd = raw_open(filename,
                             O_RDWR | O_CREAT | O_TRUNC,
                             S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // 0644
    if (fd < 0) {
        kprint("    ERROR: open() failed\n");
        return 1;
    }
    kprint("    fd assigned (should be 3)\n");

    // ── STEP 2: WRITE (first) ─────────────────────────────────────────────────
    // Kernel: copy_from_user() → page cache page marked DIRTY
    // f_pos: 0 → 29. No disk I/O — purely buffered.
    kprint("[2] Writing first message (buffered -> page cache)...\n");
    const char *msg1 = "Hello from raw syscall write\n";
    if (const long w1 = raw_write(static_cast<int>(fd), msg1, 29); w1 != 29) {
        kprint("    WARNING: partial or failed write\n");
    }

    // ── STEP 3: WRITE (second) ───────────────────────────────────────────────
    // f_pos: 29 → 62. Same dirty page updated in cache.
    kprint("[3] Writing second message...\n");
    const char *msg2 = "Second write -- offset advanced\n\n";
    const long w2 = raw_write(static_cast<int>(fd), msg2, 33);
    (void) w2;

    // ── STEP 4: LSEEK (rewind) ───────────────────────────────────────────────
    // Kernel: updates f_pos field in struct file only.
    // Zero page cache interaction. Zero I/O. Purely in-memory.
    kprint("[4] Seeking back to offset 0 (no I/O, f_pos update only)...\n");
    raw_lseek(static_cast<int>(fd), 0, SEEK_SET);

    // ── STEP 5: READ (page cache hit) ────────────────────────────────────────
    // Kernel: checks address_space radix/maple tree → page found (HIT)
    //         → copy_to_user() → f_pos advances to 62
    // No block layer, no DMA — data is still in cache from step 2/3.
    kprint("[5] Reading back (page cache HIT -- no disk I/O)...\n");
    char buf[256] = {};
    const long n = raw_read(static_cast<int>(fd), buf, sizeof(buf) - 1);
    if (n > 0) {
        kprint("    Content: ");
        syscall(SYS_write, 1, buf, n);
    }

    // ── STEP 6: LSEEK (measure file size) ────────────────────────────────────
    // SEEK_END returns current file size via f_pos. No I/O.
    const long file_size = raw_lseek(static_cast<int>(fd), 0, SEEK_END);
    kprint("[6] File size (lseek SEEK_END): measured via f_pos\n");
    (void) file_size;

    // ── STEP 7: FSYNC ────────────────────────────────────────────────────────
    // Kernel: dirty pages → bio structs → I/O scheduler
    //         → block driver programs DMA descriptors
    //         → DMA transfers page frames to device (CPU not involved)
    //         → device raises IRQ → ISR marks bios done → fsync returns
    // THIS is where DMA actually fires.
    kprint("[7] fsync() -- flushing dirty pages to disk via DMA...\n");
    if (const long sync_ret = raw_fsync(static_cast<int>(fd)); sync_ret == 0) {
        kprint("    fsync complete -- data durable on storage\n");
    }

    // ── STEP 8: CLOSE ────────────────────────────────────────────────────────
    // Kernel: fd removed from fd[] → f_count-- → if 0: struct file freed
    //         → inode i_count-- → if 0 and i_nlink==0: blocks deallocated
    kprint("[8] Closing fd -- struct file freed, inode ref dropped...\n");
    raw_close(static_cast<int>(fd));
    kprint("    Done. Run: stat journey.txt  to inspect inode.\n");

    return 0;
}
