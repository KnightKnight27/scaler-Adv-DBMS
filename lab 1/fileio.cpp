// ============================================================
//  fileio.cpp — Raw syscall file I/O (no C/C++ library I/O)
//
//  Journey:
//   1. open()  → kernel finds inode, returns fd
//   2. read()  → kernel copies pages from disk → user buffer
//   3. write() → kernel writes user buffer → page cache
//   4. close() → kernel flushes & releases fd
//
//  Syscalls used (Linux x86-64):
//   open  (SYS_open  / SYS_openat)
//   read  (SYS_read)
//   write (SYS_write)
//   close (SYS_close)
// ============================================================

#include <sys/syscall.h>   // SYS_* numbers
#include <sys/types.h>     // mode_t, ssize_t
#include <fcntl.h>         // O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <unistd.h>        // STDOUT_FILENO, STDERR_FILENO

// ── thin syscall wrappers (no libc) ─────────────────────────

static long sys_open(const char* path, int flags, int mode = 0) {
    // openat(AT_FDCWD, path, flags, mode) — preferred on modern Linux
    return syscall(SYS_openat, AT_FDCWD, path, flags, mode);
}

static ssize_t sys_read(int fd, void* buf, size_t count) {
    return syscall(SYS_read, fd, buf, count);
}

static ssize_t sys_write(int fd, const void* buf, size_t count) {
    return syscall(SYS_write, fd, buf, count);
}

static int sys_close(int fd) {
    return syscall(SYS_close, fd);
}

// ── minimal helpers (no printf, no puts) ────────────────────

static void print(const char* msg) {
    const char* p = msg;
    while (*p) p++;                         // strlen
    sys_write(STDOUT_FILENO, msg, p - msg);
}

static void print_err(const char* msg) {
    const char* p = msg;
    while (*p) p++;
    sys_write(STDERR_FILENO, msg, p - msg);
}

// convert long to decimal string into buf; returns pointer to start
static char* ltoa(long n, char* buf, int bufsz) {
    buf[--bufsz] = '\0';
    if (n == 0) { buf[--bufsz] = '0'; return buf + bufsz; }
    bool neg = (n < 0);
    if (neg) n = -n;
    while (n && bufsz > 1) {
        buf[--bufsz] = '0' + (n % 10);
        n /= 10;
    }
    if (neg && bufsz > 1) buf[--bufsz] = '-';
    return buf + bufsz;
}

// ── main ─────────────────────────────────────────────────────

int main() {

    // ══════════════════════════════════════════════════════════
    //  STEP 1 — OPEN INPUT FILE FOR READING
    //
    //  Journey:
    //   • User-space calls openat(AT_FDCWD, path, O_RDONLY)
    //   • Kernel resolves path through the directory-entry cache
    //     (dcache). If not cached, walks the filesystem tree on disk.
    //   • Kernel locates the inode (metadata block: size, permissions,
    //     block pointers).
    //   • Kernel checks read permission against process UID/GID.
    //   • Kernel allocates a file-description struct and a file
    //     descriptor (fd) — the smallest non-negative integer not
    //     already open in this process's fd table.
    //   • Returns fd back to user-space.
    // ══════════════════════════════════════════════════════════

    print("[STEP 1] Opening input.txt for reading...\n");

    long fd_in = sys_open("input.txt", O_RDONLY);
    if (fd_in < 0) {
        print_err("[ERROR] Could not open input.txt\n");
        return 1;
    }

    char fd_buf[32];
    print("[INFO]  input.txt fd = ");
    print(ltoa(fd_in, fd_buf, sizeof fd_buf));
    print("\n");


    // ══════════════════════════════════════════════════════════
    //  STEP 2 — READ FILE CONTENTS
    //
    //  Journey:
    //   • User-space calls read(fd, buf, count).
    //   • Kernel checks the file-position pointer (offset 0 initially).
    //   • Kernel looks up the page cache for the file's pages.
    //     - Cache HIT  → data copied directly from kernel page → user buf.
    //     - Cache MISS → kernel issues block I/O (e.g., via NVMe queue),
    //                    waits for interrupt, fills page cache, then copies.
    //   • File-position pointer advances by bytes actually read.
    //   • Returns number of bytes copied (≤ count).  0 = EOF.  -1 = error.
    // ══════════════════════════════════════════════════════════

    print("[STEP 2] Reading from input.txt...\n");

    char buffer[4096];
    ssize_t bytes_read = 0;
    ssize_t total_read = 0;

    // loop in case file > 4096 bytes
    while (true) {
        bytes_read = sys_read((int)fd_in, buffer + total_read,
                              sizeof(buffer) - 1 - total_read);
        if (bytes_read <= 0) break;
        total_read += bytes_read;
        if (total_read >= (ssize_t)(sizeof(buffer) - 1)) break;
    }
    buffer[total_read] = '\0';

    print("[INFO]  Bytes read = ");
    print(ltoa(total_read, fd_buf, sizeof fd_buf));
    print("\n");
    print("[INFO]  Content:\n---\n");
    sys_write(STDOUT_FILENO, buffer, total_read);
    print("\n---\n");


    // ══════════════════════════════════════════════════════════
    //  STEP 3 — CLOSE INPUT FILE
    //
    //  Journey:
    //   • close(fd) tells the kernel this process is done with fd.
    //   • Kernel decrements the reference count on the file-description.
    //   • If count reaches 0, kernel frees the file-description struct.
    //   • fd integer is released back to the process's fd table for reuse.
    //   • (Data already in page cache is NOT flushed to disk here —
    //      that happens via pdflush/writeback or fsync.)
    // ══════════════════════════════════════════════════════════

    print("[STEP 3] Closing input.txt...\n");
    sys_close((int)fd_in);


    // ══════════════════════════════════════════════════════════
    //  STEP 4 — OPEN OUTPUT FILE FOR WRITING
    //
    //  Journey:
    //   • openat(AT_FDCWD, path, O_WRONLY|O_CREAT|O_TRUNC, 0644)
    //   • O_CREAT  → if inode doesn't exist, kernel allocates a new one,
    //                writes it to the filesystem journal (ext4/xfs/btrfs),
    //                adds a directory entry in the parent directory.
    //   • O_TRUNC  → if file exists, kernel sets its size to 0 and marks
    //                all data blocks as free (CoW filesystems like btrfs
    //                do this differently).
    //   • 0644     → permissions: owner rw-, group r--, others r--.
    //   • Returns new fd.
    // ══════════════════════════════════════════════════════════

    print("[STEP 4] Opening output.txt for writing...\n");

    long fd_out = sys_open("output.txt",
                           O_WRONLY | O_CREAT | O_TRUNC,
                           0644);
    if (fd_out < 0) {
        print_err("[ERROR] Could not open output.txt\n");
        return 1;
    }

    print("[INFO]  output.txt fd = ");
    print(ltoa(fd_out, fd_buf, sizeof fd_buf));
    print("\n");


    // ══════════════════════════════════════════════════════════
    //  STEP 5 — WRITE TO OUTPUT FILE
    //
    //  Journey:
    //   • write(fd, buf, count) transitions to kernel mode (syscall).
    //   • Kernel copies data from user buffer → kernel page cache
    //     (the page is now "dirty").
    //   • File size metadata (inode) is updated in memory.
    //   • write() returns — data is NOT on disk yet (unless O_SYNC/O_DSYNC
    //     was used or fsync() is called explicitly).
    //   • The OS writeback thread (pdflush / flusher) will eventually
    //     flush dirty pages to disk based on dirty_expire_centisecs.
    // ══════════════════════════════════════════════════════════

    print("[STEP 5] Writing processed content to output.txt...\n");

    // Build output: header + original content + footer
    const char* header  = "=== Processed by syscall-fileio ===\n\n";
    const char* footer  = "\n\n=== End of file ===\n";

    ssize_t w1 = sys_write((int)fd_out, header,  __builtin_strlen(header));
    ssize_t w2 = sys_write((int)fd_out, buffer,  total_read);
    ssize_t w3 = sys_write((int)fd_out, footer,  __builtin_strlen(footer));

    ssize_t total_written = w1 + w2 + w3;
    print("[INFO]  Bytes written = ");
    print(ltoa(total_written, fd_buf, sizeof fd_buf));
    print("\n");


    // ══════════════════════════════════════════════════════════
    //  STEP 6 — CLOSE OUTPUT FILE
    //
    //  Journey:
    //   • Same as Step 3.
    //   • On some filesystems, close() may trigger a metadata flush
    //     (timestamps, size) to the journal.
    //   • Dirty page cache data still goes to disk asynchronously —
    //     use fsync(fd) before close() if durability is required.
    // ══════════════════════════════════════════════════════════

    print("[STEP 6] Closing output.txt...\n");
    sys_close((int)fd_out);

    print("[DONE]  All operations complete. Check output.txt.\n");
    return 0;
}
