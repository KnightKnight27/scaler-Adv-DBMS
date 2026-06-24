// file_io.cpp
#include <unistd.h>
#include <fcntl.h>

int main() {
    // Open file for writing (create if not exist, truncate if exist)
    // 0644 gives read/write to owner, read to others
    int fd = open("testfile.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    
    if (fd < 0) {
        char err[] = "Error opening file for write\n";
        write(2, err, 29); // 2 is stderr
        return 1;
    }

    char data[] = "System calls are working fine.\n";
    int len = 0;
    while (data[len] != '\0') len++;

    // Write to file
    write(fd, data, len);
    close(fd);

    // Now open for reading
    fd = open("testfile.txt", O_RDONLY);
    if (fd < 0) {
        char err[] = "Error opening file for read\n";
        write(2, err, 28);
        return 1;
    }

    char buffer[100];
    // Read from file into buffer
    ssize_t n = read(fd, buffer, sizeof(buffer));
    
    if (n > 0) {
        // 1 is stdout
        char prefix[] = "Read output: ";
        write(1, prefix, 13);
        write(1, buffer, n);
    }

    close(fd);
    return 0;
}
