#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main() {

    // Open the file in read-only mode
    int fd = open("file.txt", O_RDONLY);

    if (fd < 0) {
        fprintf(stderr, "Failed to open file: %s\n", strerror(errno));
        return 1;
    }

    // Buffer to store file contents
    char buffer[513];

    // Read up to 512 bytes from the file
    int bytesRead = read(fd, buffer, 512);

    if (bytesRead < 0) {
        fprintf(stderr, "Failed to read file: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    // Add null terminator if less than buffer size is read
    if (bytesRead < 512) {
        buffer[bytesRead] = '\0';
    }

    printf("Bytes read: %d\n", bytesRead);
    printf("File content:\n%s\n", buffer);

    close(fd);

    // Open file in append mode for writing
    fd = open("file.txt", O_WRONLY | O_APPEND);

    if (fd < 0) {
        fprintf(stderr, "Failed to open file for writing: %s\n", strerror(errno));
        return 1;
    }

    char text[] = "abcd\n";

    // Write data to the file
    int bytesWritten = write(fd, text, strlen(text));

    if (bytesWritten < 0) {
        fprintf(stderr, "Failed to write to file: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    printf("Bytes written: %d\n", bytesWritten);

    close(fd);

    return 0;
}