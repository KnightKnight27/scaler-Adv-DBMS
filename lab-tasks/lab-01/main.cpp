#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/stat.h>

int main() {
    const char* filename = "test_io.txt";
    const char* data = "Advanced DBMS Lab 1: System Calls and VFS\n";
    char buffer[100];

    std::cout << "1. Opening file..." << std::endl;
    // O_CREAT: create if not exists
    // O_WRONLY: write only
    // O_TRUNC: truncate to 0 if exists
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        perror("Failed to open file for writing");
        return 1;
    }

    std::cout << "2. Writing data to Page Cache..." << std::endl;
    // This writes to the kernel's Page Cache, not directly to disk (yet)
    ssize_t bytes_written = write(fd, data, strlen(data));
    if (bytes_written < 0) {
        perror("Failed to write");
        close(fd);
        return 1;
    }

    std::cout << "3. Forcing flush to physical disk (fsync)..." << std::endl;
    // Forces the VFS to flush the dirty page cache to physical storage
    if (fsync(fd) < 0) {
        perror("Failed to fsync");
    }

    std::cout << "4. Closing file descriptor..." << std::endl;
    close(fd);

    std::cout << "5. Re-opening for reading..." << std::endl;
    fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("Failed to open file for reading");
        return 1;
    }

    std::cout << "6. Reading data (likely served from Page Cache)..." << std::endl;
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read >= 0) {
        buffer[bytes_read] = '\0';
        std::cout << "Read: " << buffer;
    } else {
        perror("Failed to read");
    }

    close(fd);
    
    // Clean up
    unlink(filename);

    return 0;
}
