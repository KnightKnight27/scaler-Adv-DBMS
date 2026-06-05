#include <stdio.h>
#include <fcntl.h>      // open()
#include <unistd.h>     // read(), write(), close()
#include <string.h>     // strlen()
#include <errno.h>      // errno
#include <stdlib.h>     // exit()

#define BUFFER_SIZE 512

int main() {
    int fd;
    char buffer[BUFFER_SIZE];
    int bytesRead, bytesWritten;

    // ── 1. Open file for reading ──────────────────────────
    fd = open("file.txt", O_RDONLY);
    if (fd < 0) {
        perror("Error opening file for reading");
        exit(1);
    }
    printf("File opened for reading. File descriptor: %d\n", fd);

    // ── 2. Read data from the file ────────────────────────
    bytesRead = read(fd, buffer, BUFFER_SIZE - 1);
    if (bytesRead < 0) {
        perror("Error reading file");
        close(fd);
        exit(1);
    }
    buffer[bytesRead] = '\0'; // null terminate the buffer
    printf("\n--- File Contents ---\n%s\n", buffer);
    printf("Total bytes read: %d\n", bytesRead);

    // ── 3. Close the file ─────────────────────────────────
    if (close(fd) < 0) {
        perror("Error closing file after read");
        exit(1);
    }
    printf("\nFile closed after reading.\n");

    // ── 4. Open file in append mode ───────────────────────
    fd = open("file.txt", O_WRONLY | O_APPEND);
    if (fd < 0) {
        perror("Error opening file for append");
        exit(1);
    }
    printf("\nFile opened for appending. File descriptor: %d\n", fd);

    // ── 5. Write data to the file ─────────────────────────
    char *newData = "\nThis line was appended using write() system call.";
    bytesWritten = write(fd, newData, strlen(newData));
    if (bytesWritten < 0) {
        perror("Error writing to file");
        close(fd);
        exit(1);
    }
    printf("Total bytes written: %d\n", bytesWritten);

    // ── 6. Close the file again ───────────────────────────
    if (close(fd) < 0) {
        perror("Error closing file after write");
        exit(1);
    }
    printf("File closed after writing.\n");

    return 0;
}