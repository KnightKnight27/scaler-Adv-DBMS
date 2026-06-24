/**
 * Lab 1 — C++ File I/O Demo: Tracing the inode → VFS → Page Cache → Syscall Journey
 *
 * This program demonstrates fundamental file I/O operations that map to Linux
 * system calls. When traced with strace, each operation reveals the underlying
 * kernel path: userspace → glibc → syscall → VFS → filesystem → page cache → disk.
 *
 * Compile: g++ -std=c++17 -O0 -o file_io_demo file_io_demo.cpp
 * Trace:   strace -f -e trace=open,openat,read,write,lseek,fsync,mmap,munmap,close ./file_io_demo
 */

#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cassert>

// POSIX headers for low-level I/O
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static const char* TEST_FILE = "testfile.dat";
static const size_t PAGE_SIZE = 4096;  // typical Linux page size

// ─────────────────────────────────────────────────────
// Section 1: Sequential Write using POSIX write()
// ─────────────────────────────────────────────────────
void demo_sequential_write(int fd) {
    std::cout << "\n=== Section 1: Sequential Write (write syscall) ===" << std::endl;
    std::cout << "Writing 4 pages (16384 bytes) sequentially..." << std::endl;

    // Each write() maps to:
    //   userspace → sys_write → VFS → filesystem write → page cache (dirty page)
    // The kernel does NOT immediately flush to disk; data sits in the page cache.
    for (int i = 0; i < 4; ++i) {
        char buf[PAGE_SIZE];
        memset(buf, 'A' + i, PAGE_SIZE);  // fill each page with a different letter

        // Embed a marker at the start of each page for later verification
        std::string marker = "PAGE_" + std::to_string(i) + "_START";
        memcpy(buf, marker.c_str(), marker.size());

        ssize_t written = write(fd, buf, PAGE_SIZE);
        if (written != PAGE_SIZE) {
            std::cerr << "  ERROR: write() returned " << written
                      << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
            return;
        }
        std::cout << "  Wrote page " << i << " (filled with '" << (char)('A' + i)
                  << "', " << written << " bytes)" << std::endl;
    }

    std::cout << "  Total written: " << 4 * PAGE_SIZE << " bytes (4 pages)" << std::endl;
    std::cout << "  NOTE: Data is now in the page cache as DIRTY pages." << std::endl;
}

// ─────────────────────────────────────────────────────
// Section 2: fsync — Force Flush to Disk
// ─────────────────────────────────────────────────────
void demo_fsync(int fd) {
    std::cout << "\n=== Section 2: fsync (flush dirty pages to disk) ===" << std::endl;
    std::cout << "Calling fsync() to flush page cache to persistent storage..." << std::endl;

    // fsync() forces the kernel to:
    //   1. Write all dirty pages for this fd from the page cache to disk
    //   2. Flush the filesystem metadata (inode timestamps, size, block pointers)
    //   3. Wait until the disk controller confirms the write
    // This is the DURABILITY guarantee in ACID.
    int ret = fsync(fd);
    if (ret == -1) {
        std::cerr << "  ERROR: fsync() failed, errno=" << errno
                  << " (" << strerror(errno) << ")" << std::endl;
        return;
    }
    std::cout << "  fsync() completed — all dirty pages are now on disk." << std::endl;
    std::cout << "  Inode metadata (size, mtime, block pointers) also updated." << std::endl;
}

// ─────────────────────────────────────────────────────
// Section 3: Random Read using lseek + read
// ─────────────────────────────────────────────────────
void demo_random_read(int fd) {
    std::cout << "\n=== Section 3: Random Read (lseek + read syscalls) ===" << std::endl;

    // Read pages in reverse order to demonstrate random access.
    // The kernel path for read():
    //   sys_read → VFS → check page cache → if HIT, copy from cache
    //                                     → if MISS, schedule disk I/O, block, then copy
    for (int i = 3; i >= 0; --i) {
        off_t offset = i * PAGE_SIZE;

        // lseek() updates the file descriptor's offset in the kernel's file table.
        // It does NOT cause any disk I/O.
        off_t pos = lseek(fd, offset, SEEK_SET);
        if (pos == (off_t)-1) {
            std::cerr << "  ERROR: lseek() failed at offset " << offset << std::endl;
            return;
        }

        char buf[PAGE_SIZE];
        ssize_t bytes_read = read(fd, buf, PAGE_SIZE);
        if (bytes_read != PAGE_SIZE) {
            std::cerr << "  ERROR: read() returned " << bytes_read << std::endl;
            return;
        }

        // Verify the page marker
        std::string expected_marker = "PAGE_" + std::to_string(i) + "_START";
        bool valid = (memcmp(buf, expected_marker.c_str(), expected_marker.size()) == 0);
        std::cout << "  Read page " << i << " at offset " << offset
                  << " → marker " << (valid ? "VALID ✓" : "INVALID ✗")
                  << ", first data byte = '" << buf[expected_marker.size()] << "'" << std::endl;
    }

    std::cout << "  NOTE: Since we just wrote these pages, they were served from the PAGE CACHE" << std::endl;
    std::cout << "        (no actual disk I/O occurred for the reads)." << std::endl;
}

// ─────────────────────────────────────────────────────
// Section 4: Memory-Mapped I/O (mmap)
// ─────────────────────────────────────────────────────
void demo_mmap(int fd) {
    std::cout << "\n=== Section 4: Memory-Mapped I/O (mmap + munmap syscalls) ===" << std::endl;

    // Get file size using fstat — this reads inode metadata
    struct stat st;
    if (fstat(fd, &st) == -1) {
        std::cerr << "  ERROR: fstat() failed" << std::endl;
        return;
    }
    size_t file_size = st.st_size;
    std::cout << "  File size from inode (fstat): " << file_size << " bytes" << std::endl;

    // mmap() creates a mapping in the process's virtual address space.
    // The kernel path:
    //   sys_mmap → VFS → create VMA (Virtual Memory Area) → set up page table entries
    // Actual page loading is LAZY — pages are loaded on first access via page faults.
    void* mapped = mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        std::cerr << "  ERROR: mmap() failed, errno=" << errno
                  << " (" << strerror(errno) << ")" << std::endl;
        return;
    }
    std::cout << "  mmap() succeeded — file mapped at address " << mapped << std::endl;
    std::cout << "  Pages will be loaded lazily via page faults." << std::endl;

    // Read through the mapping — first access triggers page fault
    char* data = static_cast<char*>(mapped);
    std::cout << "  Reading through mmap:" << std::endl;
    for (int i = 0; i < 4; ++i) {
        size_t offset = i * PAGE_SIZE;
        std::string marker(data + offset, 12);  // read first 12 chars
        std::cout << "    Page " << i << " marker: \"" << marker << "\"" << std::endl;
    }

    // Write through the mapping — modifies the page cache directly
    std::cout << "  Writing 'MMAP_MODIFIED' at start of page 2 via mmap..." << std::endl;
    const char* mod = "MMAP_MODIFIED";
    memcpy(data + 2 * PAGE_SIZE, mod, strlen(mod));
    std::cout << "  Page 2 is now DIRTY in the page cache." << std::endl;

    // msync() — flush mmap modifications to disk (analogous to fsync for mmap)
    std::cout << "  Calling msync() to flush mmap changes..." << std::endl;
    if (msync(mapped, file_size, MS_SYNC) == -1) {
        std::cerr << "  ERROR: msync() failed" << std::endl;
    } else {
        std::cout << "  msync() completed — mmap changes flushed to disk." << std::endl;
    }

    // munmap() — release the mapping
    if (munmap(mapped, file_size) == -1) {
        std::cerr << "  ERROR: munmap() failed" << std::endl;
    } else {
        std::cout << "  munmap() completed — mapping released." << std::endl;
    }
}

// ─────────────────────────────────────────────────────
// Section 5: C++ iostream (high-level) vs POSIX (low-level) comparison
// ─────────────────────────────────────────────────────
void demo_cpp_iostream() {
    std::cout << "\n=== Section 5: C++ iostream vs POSIX I/O ===" << std::endl;
    std::cout << "Writing via std::ofstream (buffered, high-level)..." << std::endl;

    // std::ofstream wraps FILE* which uses stdio buffering (typically 8KB).
    // The syscall path: ofstream::write → FILE buffer → fwrite → write() syscall
    // Fewer syscalls than direct POSIX I/O because of userspace buffering.
    {
        std::ofstream ofs("iostream_test.dat", std::ios::binary);
        for (int i = 0; i < 100; ++i) {
            ofs << "Line " << i << ": This data goes through iostream buffering.\n";
        }
        // Destructor calls fclose → flushes buffer → write() → page cache
        std::cout << "  100 lines written. iostream buffered them into fewer write() syscalls." << std::endl;
    }

    // Read it back with ifstream
    {
        std::ifstream ifs("iostream_test.dat", std::ios::binary);
        ifs.seekg(0, std::ios::end);
        auto size = ifs.tellg();
        std::cout << "  Read back: file size = " << size << " bytes" << std::endl;
    }

    // Cleanup
    unlink("iostream_test.dat");
    std::cout << "  Cleaned up iostream_test.dat" << std::endl;
}

// ─────────────────────────────────────────────────────
// Section 6: Demonstrating O_DIRECT (bypassing page cache)
// ─────────────────────────────────────────────────────
void demo_o_direct() {
    std::cout << "\n=== Section 6: O_DIRECT (bypass page cache) ===" << std::endl;

    // O_DIRECT tells the kernel to bypass the page cache and do DMA directly
    // between userspace buffer and disk. This is used by databases (e.g., MySQL InnoDB)
    // that implement their own buffer pool.
    const char* direct_file = "direct_io_test.dat";
    int fd = open(direct_file, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);

    if (fd == -1) {
        std::cout << "  O_DIRECT not supported on this filesystem (common on tmpfs)." << std::endl;
        std::cout << "  Skipping O_DIRECT demo." << std::endl;
        std::cout << "  NOTE: Databases like MySQL use O_DIRECT to manage their own buffer pool," << std::endl;
        std::cout << "        avoiding double-buffering (DB buffer pool + OS page cache)." << std::endl;
        return;
    }

    // O_DIRECT requires aligned buffers and aligned I/O sizes
    void* aligned_buf = nullptr;
    if (posix_memalign(&aligned_buf, PAGE_SIZE, PAGE_SIZE) != 0) {
        std::cerr << "  ERROR: posix_memalign failed" << std::endl;
        close(fd);
        return;
    }

    memset(aligned_buf, 'D', PAGE_SIZE);
    ssize_t written = write(fd, aligned_buf, PAGE_SIZE);
    if (written == PAGE_SIZE) {
        std::cout << "  O_DIRECT write succeeded — " << written << " bytes written directly to disk." << std::endl;
        std::cout << "  This data bypassed the page cache entirely." << std::endl;
    } else {
        std::cerr << "  ERROR: O_DIRECT write failed, errno=" << errno << std::endl;
    }

    free(aligned_buf);
    close(fd);
    unlink(direct_file);
    std::cout << "  Cleaned up " << direct_file << std::endl;
}

// ─────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────
int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 1: C++ File I/O — inode → VFS → Page Cache → Syscall  ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;

    // Open the test file with POSIX open()
    // Syscall path: open() → sys_openat → VFS path lookup → inode lookup → file descriptor
    int fd = open(TEST_FILE, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        std::cerr << "FATAL: open() failed for " << TEST_FILE
                  << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
        return 1;
    }
    std::cout << "\nOpened '" << TEST_FILE << "' → fd=" << fd << std::endl;
    std::cout << "Kernel allocated: file descriptor → file table entry → inode reference" << std::endl;

    // Run all demos
    demo_sequential_write(fd);
    demo_fsync(fd);
    demo_random_read(fd);
    demo_mmap(fd);

    // Close the POSIX fd
    std::cout << "\n=== Closing POSIX file descriptor ===" << std::endl;
    close(fd);
    std::cout << "close() called → fd released, inode ref count decremented." << std::endl;

    // Additional demos
    demo_cpp_iostream();
    demo_o_direct();

    // Cleanup
    unlink(TEST_FILE);
    std::cout << "\n=== Cleanup ===" << std::endl;
    std::cout << "unlink() called → directory entry removed." << std::endl;
    std::cout << "If no other hard links exist, inode and data blocks will be freed." << std::endl;

    std::cout << "\n╔══════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  Lab 1 Complete! Run with strace to see the syscall trace.  ║" << std::endl;
    std::cout << "╚══════════════════════════════════════════════════════════════╝" << std::endl;
    return 0;
}
