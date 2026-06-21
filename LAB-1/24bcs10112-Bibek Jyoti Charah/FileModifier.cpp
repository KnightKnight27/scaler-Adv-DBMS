#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>  

int main() {
    const char* filename = "testFile.txt";

    int fd = open(filename, O_RDWR | O_CREAT, 0644);

    if (fd == -1) {
        std::cerr << "Failed to open file\n";
        return -1;
    }

    char buffer[1024];
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer) -1);

    if (bytesRead == -1) {
        std::cerr << "File read failed\n";
        close(fd);
        return -1;
    }

    buffer[bytesRead] = '\0';

    std::cout << "Original content of file:\n" << buffer << "\n";

    const char* newText = "The CPP file has modified this file through kernel level system calls\n";
    
    lseek(fd, 0, SEEK_SET);

    ssize_t bytesWritten = write(fd, newText, strlen(newText));

    if (bytesWritten==-1) {
        std::cerr << "Write to file failed\n";
        close(fd);
        return -1;
    }

    if (fsync(fd) == -1) {
        std::cerr << "Syncing with file failed\n";
        close(fd);
        return -1;
    }

    close(fd);

    std::cout << "File successfully modified and saved\n";

    return 0;
}