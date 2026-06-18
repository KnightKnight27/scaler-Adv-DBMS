#include <unistd.h>
#include <fcntl.h>

int main() {
    int fd_write = open("input.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd_write == -1) {
        return 1;
    }

    const char message[] = "Hello, this file was created using raw system calls.\n";

    write(fd_write, message, sizeof(message) - 1);

    close(fd_write);

    int fd_read = open("input.txt", O_RDONLY);

    if (fd_read == -1) {
        return 1;
    }

    int fd_output = open("output.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd_output == -1) {
        close(fd_read);
        return 1;
    }

    char buffer[100];

    int bytes_read = read(fd_read, buffer, sizeof(buffer));

    if (bytes_read > 0) {
        write(fd_output, buffer, bytes_read);
    }

    close(fd_read);
    close(fd_output);

    return 0;
}