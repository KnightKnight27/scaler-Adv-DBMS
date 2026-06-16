#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

using namespace std;

int main() {
    cout << "========================================\n";
    cout << "LAB 1: C++ FILE I/O LOW-LEVEL SYSTEM CALLS\n";
    cout << "========================================\n";

    const char* filepath = "sample_io_test.txt";
    
    // 1. Open File: using low-level open syscall
    // O_CREAT: Create file if it doesn't exist
    // O_WRONLY: Open for writing only
    // O_TRUNC: Truncate file to 0 length if it exists
    // S_IRUSR | S_IWUSR: Permissions (Read/Write for owner)
    int fd = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        cerr << "Error opening file!" << endl;
        return 1;
    }
    cout << "[Syscall] open() successful. File descriptor: " << fd << "\n";

    // Data buffer to write
    string data = "Advanced DBMS Lab 1: Low-level file I/O tracing with strace.\n"
                  "Tracing the path: User Buffer -> write() -> VFS -> Page Cache -> Inode block allocation -> Disk block write.\n";

    // 2. Write File: using write syscall
    ssize_t bytes_written = write(fd, data.c_str(), data.length());
    if (bytes_written < 0) {
        cerr << "Error writing data!" << endl;
        close(fd);
        return 1;
    }
    cout << "[Syscall] write() successful. Bytes written: " << bytes_written << "\n";

    // 3. Flush Buffer: using fsync syscall to force dirty pages from page cache to disk
    int fsync_res = fsync(fd);
    if (fsync_res < 0) {
        cerr << "Error syncing file cache!" << endl;
        close(fd);
        return 1;
    }
    cout << "[Syscall] fsync() successful. Dirty buffers flushed to physical storage.\n";

    // 4. Close File: using close syscall
    int close_res = close(fd);
    if (close_res < 0) {
        cerr << "Error closing file!" << endl;
        return 1;
    }
    cout << "[Syscall] close() successful.\n";
    cout << "========================================\n";

    return 0;
}
