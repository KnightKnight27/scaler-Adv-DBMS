// =============================================================================
//  syscall_io.cpp
//  Raw Linux System Call File I/O
//
//  Author : Anushka Jain | 24BCS10193
//  Course : Advanced DBMS (Scaler)
//  Date   : 2026-05-08
//
//  Compiled with: g++ -nostdlib -static -o syscall_io syscall_io.cpp
//  ZERO standard library. ZERO libc. ZERO runtime.
//  Every I/O operation goes through the raw SYSCALL instruction.
// =============================================================================

// ---- Kernel ABI constants (normally from <fcntl.h> and <unistd.h>) ---------
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT   0100   // octal
#define O_TRUNC  01000   // octal
#define O_APPEND 02000   // octal

// ---- Syscall numbers for x86-64 Linux --------------------------------------
#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_EXIT   60

// ---- File descriptors -------------------------------------------------------
#define STDOUT_FD   1
#define STDERR_FD   2

// ---- File mode (owner rw, group r, other r) ---------------------------------
#define FILE_MODE  0644

// =============================================================================
//  Inline assembly wrappers for raw syscalls
// =============================================================================

// Generic 1-argument syscall
static inline long syscall1(long number, long a1)
{
    long result;
    __asm__ volatile (
        "syscall"
        : "=a"(result)
        : "0"(number), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return result;
}

// Generic 3-argument syscall
static inline long syscall3(long number, long a1, long a2, long a3)
{
    long result;
    __asm__ volatile (
        "syscall"
        : "=a"(result)
        : "0"(number), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return result;
}

// =============================================================================
//  Thin wrappers that mirror libc function signatures
// =============================================================================

static long raw_open(const char *path, long flags, long mode)
{
    return syscall3(SYS_OPEN, (long)path, flags, mode);
}

static long raw_write(long fd, const void *buf, long count)
{
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}

static long raw_read(long fd, void *buf, long count)
{
    return syscall3(SYS_READ, fd, (long)buf, count);
}

static long raw_close(long fd)
{
    return syscall1(SYS_CLOSE, fd);
}

[[noreturn]] static void raw_exit(long status)
{
    syscall1(SYS_EXIT, status);
    __builtin_unreachable();
}

// =============================================================================
//  Minimal string utilities (no libc allowed)
// =============================================================================

static long str_len(const char *s)
{
    long n = 0;
    while (s[n]) ++n;
    return n;
}

// Write a null-terminated string to a file descriptor
static void write_str(long fd, const char *s)
{
    raw_write(fd, s, str_len(s));
}

// Write a long integer as decimal text
static void write_long(long fd, long v)
{
    if (v < 0) { write_str(fd, "-"); v = -v; }
    char buf[20];
    int  i = 20;
    buf[--i] = '\0';
    if (v == 0) { buf[--i] = '0'; }
    while (v > 0) { buf[--i] = '0' + (v % 10); v /= 10; }
    write_str(fd, buf + i);
}

// =============================================================================
//  Banner / logging helpers
// =============================================================================

static void banner()
{
    write_str(STDOUT_FD,
        "\n"
        "================================================================\n"
        "  syscall_io  --  Raw Linux System Call File I/O Demo\n"
        "  Author : Anushka Jain | 24BCS10193\n"
        "  No libc. No stdlib. No printf. Only SYSCALL.\n"
        "================================================================\n\n");
}

static void section(const char *title)
{
    write_str(STDOUT_FD, "\n[");
    write_str(STDOUT_FD, title);
    write_str(STDOUT_FD, "]\n");
    write_str(STDOUT_FD, "----------------------------------------------------------------\n");
}

static void step(int n, const char *desc)
{
    write_str(STDOUT_FD, "  Step ");
    write_long(STDOUT_FD, n);
    write_str(STDOUT_FD, " : ");
    write_str(STDOUT_FD, desc);
    write_str(STDOUT_FD, "\n");
}

static void ok(const char *label, long val)
{
    write_str(STDOUT_FD, "           => ");
    write_str(STDOUT_FD, label);
    write_long(STDOUT_FD, val);
    write_str(STDOUT_FD, "  [OK]\n");
}

static void err_exit(const char *msg, long code)
{
    write_str(STDERR_FD, "[ERR] ");
    write_str(STDERR_FD, msg);
    write_str(STDERR_FD, " (code=");
    write_long(STDERR_FD, code);
    write_str(STDERR_FD, ")\n");
    raw_exit(1);
}

// =============================================================================
//  Main entry point  (_start, not main -- no C runtime)
// =============================================================================

extern "C" void _start()
{
    // -------------------------------------------------------------------------
    //  Data we will write to disk
    // -------------------------------------------------------------------------
    const char *FILENAME = "syscall_io_data.txt";
    const char *PAYLOAD  =
        "Hello from syscall_io!\n"
        "Written by Anushka Jain (24BCS10193) using raw Linux syscalls.\n"
        "Path: user space -> SYSCALL -> VFS -> page cache -> disk.\n"
        "No libc. No fstream. No fprintf. Just RAX and the kernel.\n";
    const long PAYLOAD_LEN = str_len(PAYLOAD);

    banner();

    // =========================================================================
    //  PHASE 1 : WRITE PATH
    // =========================================================================
    section("PHASE 1 : WRITE PATH");

    // -- Step 1 : open (write) ------------------------------------------------
    step(1, "open(\"" "syscall_io_data.txt" "\", O_WRONLY|O_CREAT|O_TRUNC, 0644)");
    write_str(STDOUT_FD,
        "           VFS resolves the path through dentry cache -> inode.\n"
        "           O_CREAT  : allocate a fresh inode if the file is absent.\n"
        "           O_TRUNC  : discard any existing content.\n"
        "           Kernel returns the lowest free slot in the process fd table.\n"
        "           Issuing SYSCALL 2 (Ring 3 -> Ring 0) ...\n");

    long wfd = raw_open(FILENAME, O_WRONLY | O_CREAT | O_TRUNC, FILE_MODE);
    if (wfd < 0) err_exit("open for write failed", wfd);
    ok("fd = ", wfd);

    // -- Step 2 : write -------------------------------------------------------
    step(2, "write(fd, payload, len)");
    write_str(STDOUT_FD,
        "           Kernel validates fd and checks the user-space pointer.\n"
        "           Calls the filesystem's write_iter (e.g. ext4_file_write_iter).\n"
        "           copy_from_user() moves bytes into a kernel PAGE CACHE frame.\n"
        "           Page is marked DIRTY; writeback thread will flush later.\n"
        "           Issuing SYSCALL 1 (Ring 3 -> Ring 0) ...\n");

    long written = raw_write(wfd, PAYLOAD, PAYLOAD_LEN);
    if (written < 0) err_exit("write failed", written);
    ok("bytes_written = ", written);

    // -- Step 3 : close (write fd) -------------------------------------------
    step(3, "close(write_fd)");
    write_str(STDOUT_FD,
        "           Decrements struct file refcount.\n"
        "           If refcount hits zero, the filesystem .release hook fires.\n"
        "           Fd slot is freed for reuse.\n"
        "           Issuing SYSCALL 3 (Ring 3 -> Ring 0) ...\n");

    long cr = raw_close(wfd);
    if (cr < 0) err_exit("close write_fd failed", cr);
    write_str(STDOUT_FD, "           => closed  [OK]\n");

    // =========================================================================
    //  PHASE 2 : READ PATH
    // =========================================================================
    section("PHASE 2 : READ PATH");

    // -- Step 1 : open (read) -------------------------------------------------
    step(1, "open(\"" "syscall_io_data.txt" "\", O_RDONLY)");
    write_str(STDOUT_FD,
        "           VFS path lookup (likely a dentry-cache HIT this time).\n"
        "           O_RDONLY: no write permission needed.\n"
        "           Issuing SYSCALL 2 (Ring 3 -> Ring 0) ...\n");

    long rfd = raw_open(FILENAME, O_RDONLY, 0);
    if (rfd < 0) err_exit("open for read failed", rfd);
    ok("fd = ", rfd);

    // -- Step 2 : read --------------------------------------------------------
    step(2, "read(fd, buffer, 512)");
    write_str(STDOUT_FD,
        "           Kernel validates fd, checks permissions.\n"
        "           Looks up the page cache for this inode + offset.\n"
        "           PAGE CACHE HIT: data is still in RAM from the write above.\n"
        "           copy_to_user() transfers bytes directly to our buffer.\n"
        "           Zero disk I/O required (warm cache).\n"
        "           Issuing SYSCALL 0 (Ring 3 -> Ring 0) ...\n");

    char read_buf[512] = {};
    long bytes_read = raw_read(rfd, read_buf, 511);
    if (bytes_read < 0) err_exit("read failed", bytes_read);
    ok("bytes_read  = ", bytes_read);

    // -- Step 3 : close (read fd) --------------------------------------------
    step(3, "close(read_fd)");
    write_str(STDOUT_FD, "           Issuing SYSCALL 3 (Ring 3 -> Ring 0) ...\n");
    long cr2 = raw_close(rfd);
    if (cr2 < 0) err_exit("close read_fd failed", cr2);
    write_str(STDOUT_FD, "           => closed  [OK]\n");

    // =========================================================================
    //  PHASE 3 : VERIFICATION
    // =========================================================================
    section("PHASE 3 : VERIFICATION");

    write_str(STDOUT_FD, "  Data read back from disk:\n");
    write_str(STDOUT_FD, "  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n  ");
    raw_write(STDOUT_FD, read_buf, bytes_read);
    write_str(STDOUT_FD, "  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n\n");

    write_str(STDOUT_FD, "  Bytes written : ");
    write_long(STDOUT_FD, written);
    write_str(STDOUT_FD, "\n  Bytes read    : ");
    write_long(STDOUT_FD, bytes_read);
    write_str(STDOUT_FD, "\n  Match         : ");
    write_str(STDOUT_FD, (written == bytes_read) ? "YES\n" : "NO (check alignment)\n");

    write_str(STDOUT_FD,
        "\n================================================================\n"
        "  Journey complete.  All I/O used raw Linux syscalls only.\n"
        "================================================================\n\n");

    raw_exit(0);
}
