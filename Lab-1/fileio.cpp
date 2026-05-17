// ============================================================
//  fileio.cpp
//
//  Reads input.txt and writes output.txt using ONLY raw POSIX
//  syscall wrappers from <unistd.h> and <fcntl.h>.
//
//  No <stdio.h>, no <iostream>, no <fstream>, no printf.
//
//  Why unistd.h is "raw enough":
//    The functions read/write/open/close defined here are not
//    buffered library I/O. libc just provides the few lines of
//    glue that put arguments in registers and trigger the kernel
//    SYSCALL instruction — there is no user-space buffering,
//    no format parsing, no locale handling.
//
//  Author: Krritin Keshan (24bcs10122)
// ============================================================

#include <fcntl.h>      // O_RDONLY, O_WRONLY, O_CREAT, O_TRUNC
#include <unistd.h>     // read, write, close, STDOUT_FILENO, STDERR_FILENO
#include <sys/stat.h>   // file mode bits
#include <sys/types.h>  // ssize_t, size_t

namespace io {

// strlen without libc
static size_t length(const char* s) {
    const char* p = s;
    while (*p) ++p;
    return p - s;
}

// thin wrapper that emits a literal string to a fd
static void emit(int fd, const char* s) {
    write(fd, s, length(s));
}

// integer to ASCII (base 10) — fills buf from the right
static const char* num_to_str(long n, char* buf, size_t cap) {
    char* end = buf + cap;
    *--end = '\0';
    if (n == 0) { *--end = '0'; return end; }
    bool neg = (n < 0);
    unsigned long u = neg ? -(unsigned long)n : (unsigned long)n;
    while (u) {
        *--end = '0' + (u % 10);
        u /= 10;
    }
    if (neg) *--end = '-';
    return end;
}

}  // namespace io

int main() {
    using namespace io;

    char numbuf[32];

    // ─────────────────────────────────────────────────────────
    // [1/6] OPEN INPUT — open() syscall
    //
    // The kernel does the heavy lifting here:
    //   - walks the path through the dentry cache
    //   - finds the inode (size, perms, block pointers)
    //   - checks our process can read it
    //   - allocates a struct file and a file descriptor (fd)
    // The fd that comes back is just a small integer — an index
    // into this process's open-file table.
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[1/6] opening input.txt for reading\n");
    int in_fd = open("input.txt", O_RDONLY);
    if (in_fd < 0) {
        emit(STDERR_FILENO, "      could not open input.txt — exiting\n");
        return 1;
    }
    emit(STDOUT_FILENO, "      got fd = ");
    emit(STDOUT_FILENO, num_to_str(in_fd, numbuf, sizeof numbuf));
    emit(STDOUT_FILENO, "\n");

    // ─────────────────────────────────────────────────────────
    // [2/6] READ — read() syscall
    //
    // read() asks the kernel to copy bytes from the file into
    // our buffer. Path under the hood:
    //   - kernel checks the page cache
    //   - hit  -> bytes are copied from a kernel page to our buf
    //   - miss -> block layer issues an I/O request, the process
    //             sleeps until the device returns data
    // The file offset advances by the number of bytes returned.
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[2/6] reading bytes from input.txt\n");
    char buffer[8192];
    ssize_t total = 0;
    ssize_t got;
    while ((got = read(in_fd, buffer + total,
                       sizeof(buffer) - 1 - total)) > 0) {
        total += got;
        if (total >= (ssize_t)(sizeof(buffer) - 1)) break;
    }
    buffer[total] = '\0';

    emit(STDOUT_FILENO, "      bytes read = ");
    emit(STDOUT_FILENO, num_to_str(total, numbuf, sizeof numbuf));
    emit(STDOUT_FILENO, "\n");
    emit(STDOUT_FILENO, "      ----- begin file content -----\n");
    write(STDOUT_FILENO, buffer, total);
    emit(STDOUT_FILENO, "\n      ----- end file content -----\n");

    // ─────────────────────────────────────────────────────────
    // [3/6] CLOSE INPUT — close() syscall
    //
    // close() drops the reference count on the file description.
    // When it hits zero the kernel frees the description and
    // returns the fd integer to the free pool. Note: dirty pages
    // are NOT flushed by close() — that happens via writeback or
    // an explicit fsync().
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[3/6] closing input.txt\n");
    close(in_fd);

    // ─────────────────────────────────────────────────────────
    // [4/6] OPEN OUTPUT — open() with O_CREAT | O_TRUNC
    //
    //   O_CREAT  -> create the file if it does not exist; kernel
    //               allocates an inode and journals the change
    //               on ext4 / xfs.
    //   O_TRUNC  -> if the file already exists, set its size to 0
    //               and release its data blocks.
    //   0644     -> permissions: owner rw-, group r--, others r--
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[4/6] opening output.txt for writing\n");
    int out_fd = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        emit(STDERR_FILENO, "      could not open output.txt — exiting\n");
        return 1;
    }
    emit(STDOUT_FILENO, "      got fd = ");
    emit(STDOUT_FILENO, num_to_str(out_fd, numbuf, sizeof numbuf));
    emit(STDOUT_FILENO, "\n");

    // ─────────────────────────────────────────────────────────
    // [5/6] WRITE — write() syscall
    //
    // write() copies the user buffer into a kernel page (now
    // marked dirty). The inode's size and mtime are updated in
    // memory. write() returns immediately — the data is NOT on
    // the physical device yet. The kernel writeback thread will
    // flush the dirty pages later, governed by
    // /proc/sys/vm/dirty_expire_centisecs (~30 s by default).
    // For durability you would call fsync(out_fd) before close().
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[5/6] writing content to output.txt\n");

    const char* head = "----- output produced by fileio (raw syscall I/O) -----\n\n";
    const char* tail = "\n----- end of file -----\n";

    ssize_t a = write(out_fd, head, length(head));
    ssize_t b = write(out_fd, buffer, total);
    ssize_t c = write(out_fd, tail, length(tail));

    emit(STDOUT_FILENO, "      bytes written = ");
    emit(STDOUT_FILENO, num_to_str(a + b + c, numbuf, sizeof numbuf));
    emit(STDOUT_FILENO, "\n");

    // ─────────────────────────────────────────────────────────
    // [6/6] CLOSE OUTPUT — close() syscall
    //
    // Same idea as step 3. On many filesystems close() may also
    // trigger a metadata flush (size, mtime) into the journal.
    // The actual data still goes to disk asynchronously unless
    // fsync() was called first.
    // ─────────────────────────────────────────────────────────
    emit(STDOUT_FILENO, "[6/6] closing output.txt\n");
    close(out_fd);

    emit(STDOUT_FILENO, "[ok]  all done — check output.txt\n");
    return 0;
}
