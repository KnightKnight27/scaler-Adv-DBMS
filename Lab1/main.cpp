#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>

void demoCppFstream() {
    std::cout << "\n=== Starting C++ fstream (Buffered) Demo ===" << std::endl;
    
    // Writing using std::ofstream
    std::ofstream outFile("fstream_test.txt");
    if (!outFile) {
        std::cerr << "Failed to open fstream_test.txt for writing." << std::endl;
        return;
    }
    std::string data = "Hello, this is a C++ fstream buffered write journey!\n";
    outFile << data;
    outFile.close();
    std::cout << "Data written and file closed." << std::endl;

    // Reading using std::ifstream
    std::ifstream inFile("fstream_test.txt");
    if (!inFile) {
        std::cerr << "Failed to open fstream_test.txt for reading." << std::endl;
        return;
    }
    std::string line;
    std::getline(inFile, line);
    std::cout << "Read line: " << line << std::endl;
    inFile.close();
}

void demoPosixSyscalls() {
    std::cout << "\n=== Starting POSIX System Call (Direct) Demo ===" << std::endl;

    // Writing using low-level open() and write()
    int fd = open("syscall_test.txt", O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        std::perror("Error opening syscall_test.txt");
        return;
    }
    std::cout << "File opened. File descriptor: " << fd << std::endl;

    const char* text = "Hello, this is a POSIX low-level write system call journey!\n";
    ssize_t bytesWritten = write(fd, text, std::strlen(text));
    if (bytesWritten < 0) {
        std::perror("Error writing to file");
        close(fd);
        return;
    }
    std::cout << "Bytes written: " << bytesWritten << std::endl;

    if (close(fd) < 0) {
        std::perror("Error closing file descriptor");
        return;
    }
    std::cout << "File descriptor closed." << std::endl;

    // Reading using low-level open() and read()
    fd = open("syscall_test.txt", O_RDONLY);
    if (fd < 0) {
        std::perror("Error opening syscall_test.txt for reading");
        return;
    }

    char buffer[128];
    std::memset(buffer, 0, sizeof(buffer));
    ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
    if (bytesRead < 0) {
        std::perror("Error reading file");
        close(fd);
        return;
    }
    std::cout << "Bytes read: " << bytesRead << std::endl;
    std::cout << "Content read: " << buffer;

    close(fd);
}

int main() {
    std::cout << "Scaler Advanced DBMS - Lab 1: C++ File I/O & System Calls" << std::endl;
    demoCppFstream();
    demoPosixSyscalls();
    return 0;
}
