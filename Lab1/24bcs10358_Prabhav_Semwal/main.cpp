#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

int main(){
    const char* filename = "example.txt";

    // 1. Open (create if not exists)
    int fd = open(filename, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("open failed");
        return 1;
    }

    //2. Write to file
    const char* msg = "Hello from system calls!\n";
    ssize_t bytes_written = write(fd, msg, strlen(msg));
    if (bytes_written < 0) {
        perror("write failed");
        close(fd);
        return 1;
    }

    // Move file pointer back to start
    lseek(fd, 0, SEEK_SET);

    // 3. Read from file
    char buffer[100];
    ssize_t bytes_read = read(fd, buffer, sizeof(buffer) - 1);
    if (bytes_read < 0) {
        perror("read failed");
        close(fd);
        return 1;
    }

    buffer[bytes_read] = '\0'; // null terminate

    std::cout << "Read from file:\n" << buffer;

    // 4. close file
    close(fd);

    return 0;
}
