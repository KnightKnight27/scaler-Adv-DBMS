/*
 * =============================================================================
 *  warp si -- Raw System Call File I/O Demo
 * =============================================================================
 *
 *  Course  : Advanced DBMS (Scaler)
 *  Author  : Praveen
 *  Date    : 2026-05-04
 *
 *  Purpose : Demonstrate file write and read operations using ONLY raw Linux
 *            system calls (no libc wrappers, no <cstdio>, no <fstream>).
 *            Each operation is documented inline to trace the full journey
 *            a byte takes from user space down to the kernel and back.
 *
 *  Build   : g++ -nostdlib -static -o warp_si warp_si.cpp
 *              (runs without any C runtime; entry point is _start)
 *
 *  NOTE    : This program targets x86-64 Linux.  System call numbers and
 *            the syscall ABI are architecture-specific.
 * =============================================================================
 */

/* ---------------------------------------------------------------------------
 * We intentionally avoid every standard header.  All constants, types, and
 * helper functions are defined from scratch so the ONLY external dependency
 * is the Linux kernel itself.
 * --------------------------------------------------------------------------- */


/* ========================  SYSTEM CALL NUMBERS (x86-64)  =================== */
/*
 * These magic numbers come straight from the Linux kernel source:
 *   arch/x86/entry/syscalls/syscall_64.tbl
 *
 * Each number identifies a specific service the kernel exposes.
 */

#define SYS_READ   0   /* ssize_t read(int fd, void *buf, size_t count)        */
#define SYS_WRITE  1   /* ssize_t write(int fd, const void *buf, size_t count) */
#define SYS_OPEN   2   /* int open(const char *path, int flags, mode_t mode)   */
#define SYS_CLOSE  3   /* int close(int fd)                                    */
#define SYS_EXIT   60  /* void exit(int status)                                */


/* ========================  OPEN FLAGS & FILE MODES  ======================== */
/*
 * These bit-flags mirror the kernel's include/uapi/asm-generic/fcntl.h.
 * They tell the kernel HOW to open the file.
 */

#define O_RDONLY    0x0000   /* Open for reading only                           */
#define O_WRONLY    0x0001   /* Open for writing only                           */
#define O_CREAT     0x0040   /* Create the file if it does not exist            */
#define O_TRUNC     0x0200   /* Truncate the file to zero length on open        */

/* File permission bits (octal).  0644 = rw-r--r--                             */
#define MODE_FILE   0644


/* ========================  FIXED-SIZE TYPES  =============================== */

typedef unsigned long  size_t;
typedef long           ssize_t;


/* ===========================================================================
 *  raw_syscall  --  the gateway from user space to kernel space
 * ===========================================================================
 *
 *  JOURNEY STEP 1: User-space program loads arguments into CPU registers
 *  and executes the SYSCALL instruction.
 *
 *  On x86-64 Linux the calling convention is:
 *      RAX = system call number
 *      RDI = 1st argument
 *      RSI = 2nd argument
 *      RDX = 3rd argument
 *      R10 = 4th argument  (not used in this demo)
 *
 *  The SYSCALL instruction performs a privilege-level switch:
 *      Ring 3 (user mode)  -->  Ring 0 (kernel mode)
 *
 *  The CPU saves the return address in RCX and RFLAGS in R11, then jumps
 *  to the kernel entry point stored in the MSR_LSTAR register.
 *
 *  After the kernel finishes the requested work it executes SYSRET, which
 *  reverses the privilege switch and returns the result in RAX.
 * --------------------------------------------------------------------------- */

static inline long raw_syscall(long number, long a1, long a2, long a3)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)                         /* output: RAX holds return value  */
        : "a"(number), "D"(a1), "S"(a2), "d"(a3)  /* inputs                  */
        : "rcx", "r11", "memory"            /* clobbered by SYSCALL instr.    */
    );
    return ret;
}


/* ===========================================================================
 *  Thin wrappers around raw_syscall -- one per operation we need.
 *  Each wrapper simply maps friendly C arguments to register values.
 * =========================================================================== */

/*
 * sys_write -- ask the kernel to copy bytes from our buffer to a file descriptor.
 *
 * JOURNEY STEP 2 (WRITE PATH):
 *   1. Kernel validates the file descriptor (fd) in the process's open-file table.
 *   2. Kernel verifies the user buffer pointer via access_ok().
 *   3. Kernel calls the file system's .write method (e.g., ext4_file_write_iter).
 *   4. Data lands in the page cache (kernel memory); it is NOT yet on disk.
 *   5. The kernel's pdflush / writeback threads will eventually flush dirty
 *      pages to the block device via the I/O scheduler.
 *   6. The block device driver issues the actual hardware commands (SATA/NVMe).
 *   7. Kernel returns the number of bytes written (or a negative errno).
 */
static ssize_t sys_write(int fd, const void *buf, size_t count)
{
    return (ssize_t)raw_syscall(SYS_WRITE, (long)fd, (long)buf, (long)count);
}

/*
 * sys_read -- ask the kernel to copy bytes from a file descriptor into our buffer.
 *
 * JOURNEY STEP 2 (READ PATH):
 *   1. Kernel looks up the fd in the process's file-descriptor table.
 *   2. If the requested page is in the page cache -- instant hit, no disk I/O.
 *   3. On a cache miss the kernel calls the filesystem's .read_iter, which
 *      submits a block I/O request to the elevator / I/O scheduler.
 *   4. The disk controller transfers the sector(s) via DMA into kernel buffers.
 *   5. Kernel copies data from kernel buffer to the user-space pointer via
 *      copy_to_user().
 *   6. Returns number of bytes read (0 = EOF, negative = error).
 */
static ssize_t sys_read(int fd, void *buf, size_t count)
{
    return (ssize_t)raw_syscall(SYS_READ, (long)fd, (long)buf, (long)count);
}

/*
 * sys_open -- ask the kernel to open (or create) a file and return an fd.
 *
 * JOURNEY STEP 0 (both paths):
 *   1. Kernel walks the directory path component by component (VFS path lookup).
 *   2. Each component is resolved through the dentry cache (dcache).
 *   3. If O_CREAT is set and the file does not exist, the filesystem allocates
 *      a new inode and creates a directory entry.
 *   4. Kernel allocates a struct file, links it into the process's fd table,
 *      and returns the lowest available fd number.
 */
static int sys_open(const char *path, int flags, int mode)
{
    return (int)raw_syscall(SYS_OPEN, (long)path, (long)flags, (long)mode);
}

/*
 * sys_close -- release the file descriptor.
 *
 * JOURNEY STEP 3:
 *   1. Kernel decrements the reference count on the struct file.
 *   2. If the refcount reaches zero, the kernel calls the filesystem's
 *      .release method, which may trigger a final flush.
 *   3. The fd slot in the process's table is freed for reuse.
 */
static int sys_close(int fd)
{
    return (int)raw_syscall(SYS_CLOSE, (long)fd, 0, 0);
}

/*
 * sys_exit -- terminate the process.
 */
static void sys_exit(int status)
{
    raw_syscall(SYS_EXIT, (long)status, 0, 0);
    __builtin_unreachable();
}


/* ===========================================================================
 *  Helper: compute C-string length without any library.
 * =========================================================================== */
static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n]) ++n;
    return n;
}

/* Helper: write a C-string to a file descriptor. */
static void write_str(int fd, const char *s)
{
    sys_write(fd, s, str_len(s));
}

/* Helper: convert a long to its decimal ASCII representation. */
static void long_to_str(long value, char *buf, size_t buf_size)
{
    /* Handle zero. */
    if (value == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }

    /* Handle negative numbers. */
    int negative = 0;
    unsigned long uval;
    if (value < 0) {
        negative = 1;
        uval = (unsigned long)(-value);
    } else {
        uval = (unsigned long)value;
    }

    /* Write digits in reverse. */
    char tmp[24];
    int  pos = 0;
    while (uval > 0 && pos < 22) {
        tmp[pos++] = '0' + (char)(uval % 10);
        uval /= 10;
    }

    /* Copy into buf in the correct order. */
    int i = 0;
    if (negative) buf[i++] = '-';
    while (pos > 0) buf[i++] = tmp[--pos];
    buf[i] = '\0';
}


/* ===========================================================================
 *                     STDOUT / STDERR file descriptors
 * =========================================================================== */

#define STDOUT  1
#define STDERR  2


/* ===========================================================================
 *  _start  --  program entry point (no C runtime, no main())
 * ===========================================================================
 *
 *  Because we compile with -nostdlib there is no crt0 and no main().
 *  The kernel jumps directly to _start after loading the ELF binary.
 *
 *  OVERALL JOURNEY OVERVIEW
 *  ~~~~~~~~~~~~~~~~~~~~~~~~
 *  This program performs two fundamental storage operations:
 *
 *  A) WRITE PATH
 *     User buffer  -->  syscall  -->  VFS  -->  filesystem (ext4)
 *         -->  page cache  -->  I/O scheduler  -->  block driver  -->  disk
 *
 *  B) READ PATH
 *     Disk  -->  block driver  -->  I/O scheduler  -->  page cache
 *         -->  VFS  -->  syscall  -->  user buffer
 *
 *  We trace each step with console output so the reader can follow along.
 * =========================================================================== */

extern "C" void _start()
{
    /* -----------------------------------------------------------------------
     *  PHASE 0 : Declare constants and buffers
     * ----------------------------------------------------------------------- */
    const char *file_path    = "warpsi_data.txt";
    const char *payload      =
        "Hello from warp si!\n"
        "This data was written using raw Linux syscalls.\n"
        "No libc. No fprintf. No fstream. Just registers and SYSCALL.\n";

    char read_buf[256];   /* buffer to receive data back from the kernel */

    /* -----------------------------------------------------------------------
     *  PHASE 1 : WRITE -- open, write, close
     * ----------------------------------------------------------------------- */

    write_str(STDOUT,
        "\n"
        "==============================================================\n"
        "  warp si -- Raw System Call File I/O Journey\n"
        "==============================================================\n"
        "\n"
        "[PHASE 1] WRITE PATH\n"
        "------------------------------------------------------------\n"
        "\n");

    /* --- Step 1a: Open the file for writing (create + truncate) ----------- */
    write_str(STDOUT,
        "  Step 1 : open(\"warpsi_data.txt\", O_WRONLY|O_CREAT|O_TRUNC, 0644)\n"
        "           Kernel path lookup via VFS --> dentry cache --> inode.\n"
        "           O_CREAT: allocate new inode if file absent.\n"
        "           O_TRUNC: discard existing contents.\n"
        "           Returns lowest free fd in process table.\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    int write_fd = sys_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, MODE_FILE);

    if (write_fd < 0) {
        write_str(STDERR, "  ** FAILED to open file for writing.\n");
        sys_exit(1);
    }

    write_str(STDOUT, "           Result: fd = ");
    char fd_str[24];
    long_to_str(write_fd, fd_str, sizeof(fd_str));
    write_str(STDOUT, fd_str);
    write_str(STDOUT, "  [OK]\n\n");

    /* --- Step 1b: Write the payload --------------------------------------- */
    write_str(STDOUT,
        "  Step 2 : write(fd, payload, length)\n"
        "           Kernel validates fd, checks user-space pointer.\n"
        "           Calls ext4_file_write_iter (or similar).\n"
        "           Data is copied into the PAGE CACHE (dirty page).\n"
        "           Writeback threads will flush to disk asynchronously.\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    ssize_t bytes_written = sys_write(write_fd, payload, str_len(payload));

    write_str(STDOUT, "           Result: bytes_written = ");
    char bw_str[24];
    long_to_str(bytes_written, bw_str, sizeof(bw_str));
    write_str(STDOUT, bw_str);
    write_str(STDOUT, "  [OK]\n\n");

    /* --- Step 1c: Close the file ------------------------------------------ */
    write_str(STDOUT,
        "  Step 3 : close(fd)\n"
        "           Kernel decrements struct file refcount.\n"
        "           If refcount == 0, filesystem .release is called.\n"
        "           fd slot freed for reuse.\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    int close_ret = sys_close(write_fd);

    write_str(STDOUT, "           Result: ");
    write_str(STDOUT, close_ret == 0 ? "success" : "error");
    write_str(STDOUT, "  [OK]\n\n");


    /* -----------------------------------------------------------------------
     *  PHASE 2 : READ -- open, read, close
     * ----------------------------------------------------------------------- */

    write_str(STDOUT,
        "[PHASE 2] READ PATH\n"
        "------------------------------------------------------------\n"
        "\n");

    /* --- Step 2a: Open the file for reading ------------------------------- */
    write_str(STDOUT,
        "  Step 1 : open(\"warpsi_data.txt\", O_RDONLY)\n"
        "           Kernel path lookup (likely dcache hit this time).\n"
        "           O_RDONLY: no write permission requested.\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    int read_fd = sys_open(file_path, O_RDONLY, 0);

    if (read_fd < 0) {
        write_str(STDERR, "  ** FAILED to open file for reading.\n");
        sys_exit(1);
    }

    write_str(STDOUT, "           Result: fd = ");
    long_to_str(read_fd, fd_str, sizeof(fd_str));
    write_str(STDOUT, fd_str);
    write_str(STDOUT, "  [OK]\n\n");

    /* --- Step 2b: Read contents back ------------------------------------- */
    write_str(STDOUT,
        "  Step 2 : read(fd, buffer, 256)\n"
        "           Kernel checks fd, looks up page cache.\n"
        "           PAGE CACHE HIT (data was just written).\n"
        "           copy_to_user() transfers bytes to our buffer.\n"
        "           No disk I/O needed (warm cache).\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    ssize_t bytes_read = sys_read(read_fd, read_buf, sizeof(read_buf) - 1);

    write_str(STDOUT, "           Result: bytes_read = ");
    char br_str[24];
    long_to_str(bytes_read, br_str, sizeof(br_str));
    write_str(STDOUT, br_str);
    write_str(STDOUT, "  [OK]\n\n");

    /* --- Step 2c: Close the file ----------------------------------------- */
    write_str(STDOUT,
        "  Step 3 : close(fd)\n"
        "           Executing SYSCALL (Ring3 -> Ring0) ...\n");

    sys_close(read_fd);
    write_str(STDOUT, "           Result: closed  [OK]\n\n");


    /* -----------------------------------------------------------------------
     *  PHASE 3 : Display what we read back
     * ----------------------------------------------------------------------- */

    write_str(STDOUT,
        "[PHASE 3] VERIFICATION\n"
        "------------------------------------------------------------\n"
        "\n"
        "  Data read back from file:\n"
        "  vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv\n");

    if (bytes_read > 0) {
        read_buf[bytes_read] = '\0';   /* null-terminate for safe printing */
        write_str(STDOUT, "  ");
        /* Print with indentation for each line */
        for (ssize_t i = 0; i < bytes_read; ++i) {
            char c[2] = { read_buf[i], '\0' };
            sys_write(STDOUT, c, 1);
            if (read_buf[i] == '\n' && i + 1 < bytes_read) {
                write_str(STDOUT, "  ");
            }
        }
    }

    write_str(STDOUT,
        "  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n"
        "\n"
        "  Bytes written : ");
    write_str(STDOUT, bw_str);
    write_str(STDOUT, "\n"
        "  Bytes read    : ");
    write_str(STDOUT, br_str);
    write_str(STDOUT, "\n"
        "  Match         : ");
    write_str(STDOUT, (bytes_written == bytes_read) ? "YES" : "NO");
    write_str(STDOUT,
        "\n\n"
        "==============================================================\n"
        "  Journey complete.  All I/O used raw syscalls only.\n"
        "==============================================================\n"
        "\n");


    /* -----------------------------------------------------------------------
     *  PHASE 4 : Exit cleanly
     * ----------------------------------------------------------------------- */
    sys_exit(0);
}
