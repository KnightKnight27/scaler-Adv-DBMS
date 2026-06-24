#include<iostream>
#include<fcntl.h>
#include<string.h>
#include<unistd.h>

int main() {
    
    const char* filename = "test.txt";
    int fd = ::open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if(fd==-1) {
        std::cout << "open failed" << "\n";
        return 1;
    }
    std::cout << "file FD - " << fd << "\n";
    std::cout << "Kernel allocated inode, created FD table entry" << "\n";


    const char* data = "I am writing to this file";
    ssize_t written = ::write(fd,data,strlen(data));

    if(written == -1) {
        std::cout << "write failed" << "\n";
        close(fd);
        return 1;
    }
    std::cout << "written " << written << " bytes to the file." << "\n";
    std::cout << "Kernel copied data, allocated blocks, updated inode size.\n\n";

    if(close(fd)==-1) {
        std::cout << "close failed\n";
        return 1;
    }
    std::cout << "file with fd - " << fd << " closed\n\n";

    fd = ::open(filename, O_RDONLY);

    if(fd == -1) {
        std::cout << "open failed\n";
        return 1;
    }
    std::cout << "file with fd " << fd << " opened in read only mode (same file).\n";

    char buffer[256] = {0};
    ssize_t bytes_read = ::read(fd,buffer,sizeof(buffer)-1);

    if(bytes_read==-1) {
        std::cout << "error reading bytes\n";
        close(fd);
        return 1;
    }

    std::cout << "read " << bytes_read << " bytes.\n";
    std::cout << "content : " << buffer << "\n";
    std::cout << "kernel read from disk/cache\n\n";

    if(close(fd)==-1) {
        std::cout << "error closing file\n";
        return 1;
    }


    return 0;
}