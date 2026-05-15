#include <unistd.h>
#include <fcntl.h>

// my print func
void print(const char* str) {
    int len = 0;
    while (str[len] != '\0') {
        len++;
    }
    write(1, str, len);
}

int main() {
    const char* filename = "testfile.txt";
    const char* content = "hello from raw syscalls\n";
    
    // open file
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        print("open error\n");
        return 1;
    }
    
    int len = 0;
    while (content[len] != '\0') len++;
    
    // write
    ssize_t w = write(fd, content, len);
    if (w == -1) {
        print("write error\n");
        close(fd);
        return 1;
    }
    
    // close
    close(fd);
    
    // read now
    int fd_read = open(filename, O_RDONLY);
    if (fd_read == -1) {
        print("open read error\n");
        return 1;
    }
    
    char buf[100];
    
    // read
    ssize_t r = read(fd_read, buf, 99);
    if (r == -1) {
        print("read error\n");
        close(fd_read);
        return 1;
    }
    
    // terminate
    buf[r] = '\0';
    
    print("file content:\n");
    print(buf);
    
    // close
    close(fd_read);
    
    return 0;
}
