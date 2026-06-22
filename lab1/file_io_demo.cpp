// file_io_demo.cpp
//
// LAB 1: Low-level file I/O traced with strace
// Topic: the inode -> VFS -> page cache -> syscall journey
//
// This program deliberately uses RAW POSIX syscalls (open, write, lseek,
// read, fsync, fstat, close, unlink) instead of the C++ <fstream> or the C
// stdio (FILE*) layer. The reason is pedagogical: every call below maps to
// exactly one system call, so when we run the program under strace each
// action shows up on its own line. The higher-level libraries buffer data
// in user space and would coalesce or hide many of these calls, making the
// trace much harder to read.
//
// Buffered vs direct (a note for the report):
//   - "Buffered" I/O (what we use here) means the kernel keeps a copy of the
//     file's data in the PAGE CACHE. A write() returns as soon as the bytes
//     are copied into a dirty page in RAM; the actual disk write happens
//     later during writeback, OR immediately when we call fsync().
//   - "Direct" I/O (open with O_DIRECT) bypasses the page cache and transfers
//     straight to/from the block device. It is faster only for applications
//     (like databases) that manage their own cache and want to avoid double
//     buffering. We do NOT use O_DIRECT here because it has strict alignment
//     requirements; instead we use fsync() to demonstrate forcing a flush.
//
// Build:
//   g++ -std=c++17 file_io_demo.cpp -o file_io_demo
//
// Run under strace (Linux only):
//   ./run_strace.sh

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include <fcntl.h>      // open(), O_CREAT, O_RDWR, O_TRUNC
#include <unistd.h>     // read(), write(), lseek(), fsync(), close(), unlink()
#include <sys/stat.h>   // fstat(), struct stat
#include <sys/types.h>
#include <cerrno>

namespace {

// Small helper: report a fatal syscall failure and abort. Using errno keeps
// the message accurate to whatever the kernel actually returned.
[[noreturn]] void die(const char* what) {
    std::fprintf(stderr, "[FATAL] %s failed: %s\n", what, std::strerror(errno));
    std::exit(EXIT_FAILURE);
}

} // namespace

int main() {
    const char* path = "lab1_data.bin";

    std::printf("=== Low-level file I/O demo ===\n");
    std::printf("Target file: %s\n\n", path);

    // ------------------------------------------------------------------
    // STEP 1: open()  ->  syscall openat()
    //
    // O_CREAT  : create the file if it does not exist.
    // O_RDWR   : open for both reading and writing.
    // O_TRUNC  : if it already exists, truncate it to length 0 so each run
    //            starts clean.
    // 0644     : permission bits (rw-r--r--) applied only when the file is
    //            created. Subject to the process umask.
    //
    // The kernel walks the path through the VFS using the dentry cache,
    // allocates (or finds) an inode, and returns the lowest unused file
    // descriptor for this process. On Linux open() is implemented as the
    // openat() syscall, which is what strace shows.
    // ------------------------------------------------------------------
    std::printf("[1] open(%s, O_CREAT|O_RDWR|O_TRUNC, 0644)\n", path);
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 0) die("open");
    std::printf("    -> got file descriptor fd=%d\n\n", fd);

    // ------------------------------------------------------------------
    // STEP 2: write()  ->  syscall write()
    //
    // The bytes are copied from our user-space buffer into a page in the
    // kernel PAGE CACHE associated with this file. The page is marked
    // "dirty". write() returns the number of bytes accepted; it does NOT
    // wait for the data to reach the disk.
    // ------------------------------------------------------------------
    const std::string payload =
        "Hello from the page cache!\n"
        "This text lives in a dirty page until writeback or fsync.\n";

    std::printf("[2] write(fd=%d, buf, %zu bytes)\n", fd, payload.size());
    ssize_t written = write(fd, payload.data(), payload.size());
    if (written < 0) die("write");
    std::printf("    -> wrote %zd bytes (file offset is now %zd)\n\n",
                written, written);

    // ------------------------------------------------------------------
    // STEP 3: lseek()  ->  syscall lseek()
    //
    // Each open file description has a current offset. After the write above
    // the offset sits at the end of what we wrote. To read the data back we
    // must reposition the offset to the start of the file.
    //
    // SEEK_SET means "absolute position", so offset 0 == beginning of file.
    // lseek does NOT touch the disk; it only updates kernel bookkeeping.
    // ------------------------------------------------------------------
    std::printf("[3] lseek(fd=%d, 0, SEEK_SET)\n", fd);
    off_t pos = lseek(fd, 0, SEEK_SET);
    if (pos < 0) die("lseek");
    std::printf("    -> offset reset to %lld\n\n", static_cast<long long>(pos));

    // ------------------------------------------------------------------
    // STEP 4: read()  ->  syscall read()
    //
    // Because we wrote the data moments ago, the page is already warm in the
    // page cache, so this read is satisfied from RAM with no disk access at
    // all. (On a cold cache the kernel would issue a block-layer request and
    // the disk would actually be touched.) The bytes are copied from the
    // kernel page back into our user-space buffer.
    // ------------------------------------------------------------------
    char buffer[256];
    std::memset(buffer, 0, sizeof(buffer));

    std::printf("[4] read(fd=%d, buf, %zu bytes)\n", fd, sizeof(buffer) - 1);
    ssize_t got = read(fd, buffer, sizeof(buffer) - 1);
    if (got < 0) die("read");
    std::printf("    -> read %zd bytes back:\n", got);
    std::printf("    -------- file contents --------\n%s", buffer);
    std::printf("    -------------------------------\n\n");

    // ------------------------------------------------------------------
    // STEP 5: fsync()  ->  syscall fsync()
    //
    // This is the key durability step. fsync() tells the kernel: "do not
    // return until every dirty page of this file (and its metadata) has been
    // physically flushed through the block layer to the storage device."
    // Without fsync the data could sit in the page cache and be lost if the
    // machine crashed before writeback ran. Databases call fsync (or
    // fdatasync) at commit time for exactly this reason.
    // ------------------------------------------------------------------
    std::printf("[5] fsync(fd=%d)  (force dirty pages to disk)\n", fd);
    if (fsync(fd) < 0) die("fsync");
    std::printf("    -> data is now durable on the device\n\n");

    // ------------------------------------------------------------------
    // STEP 6: fstat()  ->  syscall fstat()
    //
    // fstat reads the file's INODE metadata: its unique inode number, its
    // size in bytes, the block count, timestamps, ownership, etc. None of
    // this data lives inside the file itself; it lives in the inode, which
    // the filesystem stores separately and the VFS caches in memory.
    // ------------------------------------------------------------------
    struct stat st;
    std::printf("[6] fstat(fd=%d)\n", fd);
    if (fstat(fd, &st) < 0) die("fstat");
    std::printf("    -> inode number : %llu\n",
                static_cast<unsigned long long>(st.st_ino));
    std::printf("    -> size (bytes) : %lld\n",
                static_cast<long long>(st.st_size));
    std::printf("    -> 512B blocks  : %lld\n",
                static_cast<long long>(st.st_blocks));
    std::printf("    -> link count   : %lu\n\n",
                static_cast<unsigned long>(st.st_nlink));

    // ------------------------------------------------------------------
    // STEP 7: close()  ->  syscall close()
    //
    // close() releases the file descriptor and the underlying open file
    // description. The inode itself stays on disk; only our handle to it goes
    // away. Any remaining buffered data is scheduled for writeback (we already
    // forced it with fsync above, so nothing is at risk here).
    // ------------------------------------------------------------------
    std::printf("[7] close(fd=%d)\n", fd);
    if (close(fd) < 0) die("close");
    std::printf("    -> descriptor released\n\n");

    // ------------------------------------------------------------------
    // STEP 8 (optional): unlink()  ->  syscall unlink()
    //
    // unlink removes the directory entry (the name) that points at the inode
    // and decrements the inode's link count. When the link count reaches 0
    // and no process still has the file open, the filesystem frees the inode
    // and its data blocks. We make this opt-in so the trace can show it while
    // still letting a student inspect lab1_data.bin afterwards.
    //
    // Set the environment variable KEEP_FILE=1 to skip unlink().
    // ------------------------------------------------------------------
    const char* keep = std::getenv("KEEP_FILE");
    if (keep != nullptr && std::strcmp(keep, "1") == 0) {
        std::printf("[8] KEEP_FILE=1 set -> leaving %s on disk\n", path);
    } else {
        std::printf("[8] unlink(%s)\n", path);
        if (unlink(path) < 0) die("unlink");
        std::printf("    -> directory entry removed (inode freed when last "
                    "reference drops)\n");
    }

    std::printf("\n=== done ===\n");
    return 0;
}
