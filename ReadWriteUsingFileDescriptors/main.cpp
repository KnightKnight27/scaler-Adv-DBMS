// Lab Session 1 — Linux POSIX File I/O: Kernel Journey via strace
// Student : Indrajeet Yadav | Roll No: 23BCS10199
//
// Objective: Demonstrate every syscall in a file's full lifecycle —
//   open() → write() → fsync() → close() → open() → read() → lseek() → close()
// — and explain what the Linux kernel does at each step.
//
// Build:  g++ -std=c++17 -Wall -Wextra -O2 file_io.cpp -o file_io
// Run:    ./file_io
// Trace:  strace -e trace=openat,read,write,close,fsync,lseek,fstat ./file_io

#include <cerrno>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Prints a descriptive error and exits; every syscall result passes through here.
static void die(const char* what) {
    std::cerr << "[ERROR] " << what << " failed: "
              << std::strerror(errno) << " (errno=" << errno << ")\n";
    std::exit(1);
}

// Print a divider for clarity in stdout output
static void section(const char* title) {
    std::cout << "\n╔══════════════════════════════════════════════════╗\n"
              << "  " << title << "\n"
              << "╚══════════════════════════════════════════════════╝\n";
}

int main() {
    const char* filename = "lab1_test.txt";
    const char* payload  = "Hello from Lab 1 — Indrajeet Yadav 23BCS10199";
    const std::size_t payload_len = std::strlen(payload);

    // =========================================================
    // PHASE 1: open() for writing
    // =========================================================
    section("PHASE 1 — open() for writing (O_CREAT | O_WRONLY | O_TRUNC)");

    // O_CREAT  — create the file if it doesn't exist (uses mode 0644)
    // O_WRONLY — open only for writing
    // O_TRUNC  — if the file already exists, truncate it to size 0 first
    int fd = ::open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd == -1) die("open(O_WRONLY)");

    std::cout << "syscall : openat(AT_FDCWD, \"" << filename
              << "\", O_WRONLY|O_CREAT|O_TRUNC, 0644) = " << fd << "\n"
              << "\nKernel steps:\n"
              << "  1. Path resolution: kernel walks the VFS from AT_FDCWD (process cwd)\n"
              << "     component by component until it reaches the target filename.\n"
              << "  2. Inode lookup: each directory entry maps filename -> inode number.\n"
              << "     The inode stores metadata: size, permissions, block pointers, timestamps.\n"
              << "  3. O_CREAT: if the inode doesn't exist, allocate a new one on the\n"
              << "     filesystem (ext4/btrfs/xfs) with mode 0644 and link it in the dir.\n"
              << "  4. O_TRUNC: if the inode already exists and has data, release all\n"
              << "     data blocks and reset inode size to 0. Done atomically.\n"
              << "  5. Permission check: compare process UID/GID against inode st_uid/st_gid/st_mode.\n"
              << "  6. Allocate open file description: a kernel 'struct file' holding\n"
              << "     {flags=O_WRONLY, offset=0, pointer to inode, reference count}.\n"
              << "  7. Install fd: find the lowest free slot in this process's FD table\n"
              << "     (0=stdin, 1=stdout, 2=stderr are taken), install the description there.\n"
              << "  8. Return fd = " << fd << " to user space.\n";

    // =========================================================
    // PHASE 2: write()
    // =========================================================
    section("PHASE 2 — write() — data enters the page cache");

    ssize_t written = ::write(fd, payload, payload_len);
    if (written == -1) die("write");

    std::cout << "syscall : write(" << fd << ", payload, " << payload_len
              << ") = " << written << "\n"
              << "\nKernel steps:\n"
              << "  1. Validate fd -> locate open file description.\n"
              << "  2. Copy " << written << " bytes from USER SPACE buffer into the PAGE CACHE.\n"
              << "     The page cache is RAM keyed by (inode, page-aligned-offset).\n"
              << "     Each page is 4096 bytes (PAGE_SIZE). Our " << written
              << "-byte write fits in one page.\n"
              << "  3. Mark that page DIRTY — a kernel background thread (pdflush/\n"
              << "     kworker) will eventually flush it to disk (writeback).\n"
              << "  4. Update inode: st_size = " << written
              << ", mtime = ctime = now.\n"
              << "  5. Advance the open file description's offset by " << written << ".\n"
              << "  6. Return " << written << " (bytes accepted into cache).\n"
              << "\nIMPORTANT: Data is NOT on disk yet. If power fails now, it is LOST.\n";

    // =========================================================
    // PHASE 3: fsync()
    // =========================================================
    section("PHASE 3 — fsync() — force durability");

    if (::fsync(fd) == -1) die("fsync");

    std::cout << "syscall : fsync(" << fd << ") = 0\n"
              << "\nKernel steps:\n"
              << "  1. Locate all dirty pages belonging to this inode in the page cache.\n"
              << "  2. Submit write requests to the block device (via the I/O scheduler).\n"
              << "  3. Wait for the device to acknowledge the writes (write-back complete).\n"
              << "  4. Also flush inode metadata (size, mtime) to the journal / metadata area.\n"
              << "  5. Return 0 only when both data and metadata are durable on storage.\n"
              << "\nAfter fsync returns 0, this file survives a system crash.\n"
              << "Databases call fsync at transaction commit for exactly this reason.\n";

    // =========================================================
    // PHASE 4: close() the write fd
    // =========================================================
    section("PHASE 4 — close() — release the file descriptor");

    if (::close(fd) == -1) die("close(write fd)");

    std::cout << "syscall : close(" << fd << ") = 0\n"
              << "\nKernel steps:\n"
              << "  1. Remove fd " << fd << " from this process's FD table.\n"
              << "  2. Decrement the reference count on the open file description.\n"
              << "  3. If ref count hits 0, free the struct file (the description).\n"
              << "  4. Decrement the inode's open count.\n"
              << "  5. Does NOT imply fsync — dirty pages may still be in cache.\n"
              << "     (We already called fsync above, so we're safe here.)\n"
              << "  6. Lowest free fd slot is now available again for the next open().\n";

    // =========================================================
    // PHASE 5: open() for reading
    // =========================================================
    section("PHASE 5 — open() for reading (O_RDONLY)");

    fd = ::open(filename, O_RDONLY);
    if (fd == -1) die("open(O_RDONLY)");

    std::cout << "syscall : openat(AT_FDCWD, \"" << filename << "\", O_RDONLY) = " << fd << "\n"
              << "\nKernel steps:\n"
              << "  1. Same path resolution as Phase 1, finds the SAME inode.\n"
              << "  2. No O_CREAT / O_TRUNC — just open the existing inode.\n"
              << "  3. Allocate a FRESH open file description: offset=0, flags=O_RDONLY.\n"
              << "     This is a NEW description, separate from the Phase 1 description.\n"
              << "     That is why the offset restarts at 0 even though we wrote 25 bytes.\n"
              << "  4. Install at fd = " << fd
              << " (same number recycled — lowest free slot).\n";

    // =========================================================
    // PHASE 6: read()
    // =========================================================
    section("PHASE 6 — read() — served from page cache (no disk I/O)");

    char buffer[512] = {};
    ssize_t bytes_read = ::read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read == -1) die("read");
    buffer[bytes_read] = '\0';

    std::cout << "syscall : read(" << fd << ", buf, " << sizeof(buffer) - 1
              << ") = " << bytes_read << "\n"
              << "content : \"" << buffer << "\"\n"
              << "\nKernel steps:\n"
              << "  1. Compute which page cache pages cover offset 0.." << bytes_read-1 << ".\n"
              << "  2. Page cache HIT: the pages we wrote in Phase 2 are still in RAM\n"
              << "     (same physical memory, no disk I/O needed).\n"
              << "  3. Copy " << bytes_read << " bytes from page cache -> user buffer.\n"
              << "  4. Advance offset by " << bytes_read << ".\n"
              << "  5. Update inode atime (last access time), subject to mount options.\n"
              << "  6. Return " << bytes_read << ". Reading " << sizeof(buffer)-1
              << " bytes returned only " << bytes_read
              << " — that is EOF, not an error.\n";

    // =========================================================
    // PHASE 7: lseek() — file offset manipulation
    // =========================================================
    section("PHASE 7 — lseek() — manipulate the file offset");

    off_t current_pos = ::lseek(fd, 0, SEEK_CUR);
    std::cout << "syscall : lseek(" << fd << ", 0, SEEK_CUR) = " << current_pos
              << "  (current offset after Phase 6 read)\n"
              << "\nKernel steps:\n"
              << "  lseek() is a PURE METADATA operation. It only modifies the 'offset'\n"
              << "  field inside the open file description. Zero disk I/O.\n"
              << "  SEEK_CUR with offset=0 is the POSIX idiom for 'where am I now?'\n\n";

    // Seek back to start and re-read first 10 bytes
    ::lseek(fd, 0, SEEK_SET);
    char prefix[11] = {};
    bytes_read = ::read(fd, prefix, 10);
    prefix[bytes_read] = '\0';

    std::cout << "syscall : lseek(" << fd << ", 0, SEEK_SET) = 0  (rewind)\n"
              << "syscall : read(" << fd << ", buf, 10) = " << bytes_read
              << "\ncontent : \"" << prefix << "\"  (first 10 bytes re-read)\n"
              << "\nThis proves the offset lives in the open file description,\n"
              << "not the inode. Two fds open on the same file have independent offsets.\n";

    // =========================================================
    // PHASE 8: fstat() — inode metadata
    // =========================================================
    section("PHASE 8 — fstat() — read inode metadata");

    struct stat st;
    if (::fstat(fd, &st) == -1) die("fstat");

    std::cout << "syscall : fstat(" << fd << ", &st) = 0\n"
              << "\nInode metadata:\n"
              << "  st_ino   = " << st.st_ino   << "  (inode number — file's kernel identity)\n"
              << "  st_size  = " << st.st_size  << "  (bytes)\n"
              << "  st_mode  = 0" << std::oct << (st.st_mode & 0777) << std::dec
              << "  (permissions)\n"
              << "  st_nlink = " << st.st_nlink << "  (hard link count)\n"
              << "  st_uid   = " << st.st_uid   << "\n"
              << "  st_blksize = " << st.st_blksize << "  (preferred I/O block size)\n"
              << "\nfstat() reads from the VFS inode cache (icache) — no disk I/O when cached.\n";

    // =========================================================
    // PHASE 9: close() the read fd
    // =========================================================
    section("PHASE 9 — close() — final cleanup");

    if (::close(fd) == -1) die("close(read fd)");
    std::cout << "syscall : close(" << fd << ") = 0\n"
              << "All resources released. Inode remains on disk.\n";

    // =========================================================
    // Summary
    // =========================================================
    section("SUMMARY — Three-Level FD Model");
    std::cout
        << "  Process FD table                     Kernel                        Disk\n"
        << "  ────────────────                     ──────                        ────\n"
        << "  fd 0 → stdin\n"
        << "  fd 1 → stdout\n"
        << "  fd 2 → stderr\n"
        << "  fd 3 → open file description A  →  inode " << st.st_ino
        << "  →  data blocks\n\n"
        << "Key facts:\n"
        << "  • fd is per-process. After fork(), parent and child SHARE the same\n"
        << "    open file description (shared offset). dup() also shares it.\n"
        << "  • The file offset lives in the description, not the inode.\n"
        << "    Re-opening a file gives a fresh description with offset=0.\n"
        << "  • The inode IS the file. Renaming it does not change the inode.\n"
        << "    Deleting the directory entry while the file is open just removes\n"
        << "    the name — the inode (and data) survives until the last fd closes.\n"
        << "  • write() lands in the PAGE CACHE (RAM), not disk.\n"
        << "    fsync() is the only guarantee of durability.\n\n"
        << "Run: strace -e trace=openat,read,write,close,fsync,lseek,fstat ./file_io\n"
        << "to see every syscall this program issues at the kernel boundary.\n";

    return 0;
}
