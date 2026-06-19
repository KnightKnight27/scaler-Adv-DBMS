// ================================================================
//  fileio.cpp — Bare-metal file I/O through Linux system calls
//
//  Author: Shubham Shah (24BCS10316)
//
//  This program demonstrates direct kernel interaction for file
//  operations, deliberately avoiding all standard C/C++ I/O
//  facilities. The execution flow proceeds as follows:
//
//   Phase 1: open()  → kernel resolves path, returns descriptor
//   Phase 2: read()  → kernel fetches pages into user buffer
//   Phase 3: close() → kernel releases input descriptor
//   Phase 4: open()  → kernel creates/truncates output file
//   Phase 5: write() → kernel stages data in page cache
//   Phase 6: close() → kernel releases output descriptor
//
//  System calls leveraged (Linux x86-64 ABI):
//   SYS_openat (257) — modern variant of open
//   SYS_read   (0)   — fetch bytes from descriptor
//   SYS_write  (1)   — push bytes to descriptor
//   SYS_close  (3)   — release descriptor
// ================================================================

#include <sys/syscall.h>   // System call dispatch numbers
#include <sys/types.h>     // Portable type definitions (mode_t, ssize_t)
#include <fcntl.h>         // File control flags (O_RDONLY, O_WRONLY, etc.)
#include <unistd.h>        // Standard symbolic constants (STDOUT_FILENO)

// ── Lightweight syscall wrappers (bypassing libc entirely) ──────


static long raw_open(const char* filepath, int flags, int perms = 0) {
    // Prefer openat with AT_FDCWD — this resolves relative to the
    // current working directory, which is the modern canonical form
    return syscall(SYS_openat, AT_FDCWD, filepath, flags, perms);
}

static ssize_t raw_read(int descriptor, void* destination, size_t nbytes) {
    return syscall(SYS_read, descriptor, destination, nbytes);
}

static ssize_t raw_write(int descriptor, const void* source, size_t nbytes) {
    return syscall(SYS_write, descriptor, source, nbytes);
}

static int raw_close(int descriptor) {
    return syscall(SYS_close, descriptor);
}

// ── Utility routines (no printf, no cout, no puts) ──────────────

static void emit(const char* text) {
    // Manually compute string length since strlen is part of libc
    const char* cursor = text;
    while (*cursor) cursor++;
    raw_write(STDOUT_FILENO, text, cursor - text);
}

static void emit_error(const char* text) {
    const char* cursor = text;
    while (*cursor) cursor++;
    raw_write(STDERR_FILENO, text, cursor - text);
}

// Render a long integer as a decimal string within the provided buffer.
// Returns a pointer to the beginning of the rendered characters.
static char* render_long(long value, char* scratch, int scratch_len) {
    scratch[--scratch_len] = '\0';
    if (value == 0) {
        scratch[--scratch_len] = '0';
        return scratch + scratch_len;
    }
    bool negative = (value < 0);
    if (negative) value = -value;
    while (value && scratch_len > 1) {
        scratch[--scratch_len] = '0' + (value % 10);
        value /= 10;
    }
    if (negative && scratch_len > 1) scratch[--scratch_len] = '-';
    return scratch + scratch_len;
}

// ── Program entry point ─────────────────────────────────────────

int main() {

    // ════════════════════════════════════════════════════════════
    //  PHASE 1 — ACQUIRE INPUT FILE DESCRIPTOR
    //
    //  What happens beneath the surface:
    //   • We invoke openat(AT_FDCWD, "input.txt", O_RDONLY).
    //   • The kernel's VFS layer resolves the pathname by walking
    //     directory entries, consulting the dcache for speed.
    //   • Once the target inode is located, the kernel verifies
    //     that our process has sufficient read permissions.
    //   • A fresh file-description structure is allocated in
    //     kernel memory, and the lowest available non-negative
    //     integer is chosen as our file descriptor.
    //   • That integer is handed back to user-space.
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 1] Acquiring descriptor for input.txt...\n");

    long input_fd = raw_open("input.txt", O_RDONLY);
    if (input_fd < 0) {
        emit_error("[FATAL] Failed to open input.txt — aborting.\n");
        return 1;
    }

    char scratch[32];
    emit("[INFO]  Assigned descriptor for input.txt: ");
    emit(render_long(input_fd, scratch, sizeof scratch));
    emit("\n");


    // ════════════════════════════════════════════════════════════
    //  PHASE 2 — TRANSFER FILE CONTENTS INTO USER BUFFER
    //
    //  What happens beneath the surface:
    //   • Our read() call triggers a SYSCALL instruction, which
    //     transitions the CPU from ring 3 to ring 0.
    //   • The kernel checks the current file-position pointer
    //     (starts at byte 0 after a fresh open).
    //   • It then consults the page cache:
    //     - If the relevant pages are already cached in RAM,
    //       the kernel copies them directly to our buffer.
    //     - Otherwise, a block I/O request is dispatched to the
    //       storage driver; our process is put to sleep until
    //       the DMA transfer completes and an interrupt fires.
    //   • The file-position pointer advances by the byte count
    //     that was actually transferred.
    //   • Return value: bytes read (>0), EOF (0), or error (-1).
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 2] Extracting contents from input.txt...\n");

    char data_buf[4096];
    ssize_t chunk_size = 0;
    ssize_t accumulated = 0;

    // Iterative read loop to handle files potentially exceeding
    // a single buffer-fill (though 4 KB suffices for our sample)
    while (true) {
        chunk_size = raw_read((int)input_fd, data_buf + accumulated,
                              sizeof(data_buf) - 1 - accumulated);
        if (chunk_size <= 0) break;
        accumulated += chunk_size;
        if (accumulated >= (ssize_t)(sizeof(data_buf) - 1)) break;
    }
    data_buf[accumulated] = '\0';

    emit("[INFO]  Total bytes extracted: ");
    emit(render_long(accumulated, scratch, sizeof scratch));
    emit("\n");
    emit("[INFO]  Extracted content:\n---\n");
    raw_write(STDOUT_FILENO, data_buf, accumulated);
    emit("\n---\n");


    // ════════════════════════════════════════════════════════════
    //  PHASE 3 — RELINQUISH INPUT DESCRIPTOR
    //
    //  What happens beneath the surface:
    //   • close(fd) decrements the kernel's reference counter
    //     on the associated file-description structure.
    //   • Once the counter hits zero, the structure is freed
    //     and the descriptor number becomes available for reuse.
    //   • Important: this does NOT force cached data to disk.
    //     Writeback is handled independently by kernel threads.
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 3] Releasing input.txt descriptor...\n");
    raw_close((int)input_fd);


    // ════════════════════════════════════════════════════════════
    //  PHASE 4 — ESTABLISH OUTPUT FILE DESCRIPTOR
    //
    //  What happens beneath the surface:
    //   • openat(AT_FDCWD, "output.txt", O_WRONLY|O_CREAT|O_TRUNC, 0644)
    //   • O_CREAT: if the target inode is absent, the kernel
    //     provisions a new one, commits it to the filesystem
    //     journal for crash resilience, and registers a directory
    //     entry in the parent directory.
    //   • O_TRUNC: if the file already exists, its size is reset
    //     to zero and all previously allocated data blocks are
    //     reclaimed. (CoW filesystems handle this via reflinks.)
    //   • 0644: sets the permission mask — rw-r--r--.
    //   • A new descriptor integer is returned.
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 4] Establishing descriptor for output.txt...\n");

    long output_fd = raw_open("output.txt",
                              O_WRONLY | O_CREAT | O_TRUNC,
                              0644);
    if (output_fd < 0) {
        emit_error("[FATAL] Failed to open output.txt — aborting.\n");
        return 1;
    }

    emit("[INFO]  Assigned descriptor for output.txt: ");
    emit(render_long(output_fd, scratch, sizeof scratch));
    emit("\n");


    // ════════════════════════════════════════════════════════════
    //  PHASE 5 — COMMIT DATA TO OUTPUT FILE
    //
    //  What happens beneath the surface:
    //   • write(fd, buf, count) enters kernel mode via SYSCALL.
    //   • The kernel copies our user-space data into page cache
    //     pages, which are then flagged as "dirty."
    //   • The inode's size and mtime fields are refreshed.
    //   • write() returns promptly — data is NOT yet on disk.
    //     This is the write-back (deferred persistence) model.
    //   • Kernel flusher threads will eventually sync dirty pages
    //     to the storage device, controlled by the tunable
    //     vm.dirty_expire_centisecs (default ~30 seconds).
    //   • For strict durability, one would call fsync(fd) before
    //     invoking close().
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 5] Committing processed content to output.txt...\n");

    // Construct output: banner → original payload → closing marker
    const char* banner  = "=== Output generated by fileio (Shubham Shah, 24BCS10316) ===\n\n";
    const char* closing = "\n\n=== Processing complete ===\n";

    ssize_t written_banner  = raw_write((int)output_fd, banner,  __builtin_strlen(banner));
    ssize_t written_payload = raw_write((int)output_fd, data_buf, accumulated);
    ssize_t written_closing = raw_write((int)output_fd, closing, __builtin_strlen(closing));

    ssize_t total_committed = written_banner + written_payload + written_closing;
    emit("[INFO]  Total bytes committed: ");
    emit(render_long(total_committed, scratch, sizeof scratch));
    emit("\n");


    // ════════════════════════════════════════════════════════════
    //  PHASE 6 — FINALIZE OUTPUT DESCRIPTOR
    //
    //  What happens beneath the surface:
    //   • Mirrors Phase 3 — descriptor is released.
    //   • Depending on the filesystem, closing may trigger a
    //     journal flush for metadata (timestamps, file size).
    //   • Dirty data pages still proceed to disk asynchronously
    //     unless fsync() was explicitly invoked earlier.
    // ════════════════════════════════════════════════════════════

    emit("[PHASE 6] Finalizing output.txt descriptor...\n");
    raw_close((int)output_fd);

    emit("[COMPLETE] All file operations finished successfully. Inspect output.txt for results.\n");
    return 0;
}