#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 512
#define FILENAME "file.txt"

int main() {
    int fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytesRead, bytesWritten;

    // Step 1: Create file with initial content
    fd = open(FILENAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error creating file");
        exit(EXIT_FAILURE);
    }

    const char *initialContent =
        "Hello, this is the initial content of the file.\n"
        "Linux system calls give direct kernel-level file control.\n";
    bytesWritten = write(fd, initialContent, strlen(initialContent));
    if (bytesWritten == -1) {
        perror("Error writing to file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("Initial write: %zd bytes written\n", bytesWritten);

    if (close(fd) == -1) {
        perror("Error closing file after initial write");
        exit(EXIT_FAILURE);
    }

    // Step 2: Open file for reading
    fd = open(FILENAME, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file for reading");
        exit(EXIT_FAILURE);
    }

    // Step 3: Read data from the file
    bytesRead = read(fd, buffer, BUFFER_SIZE - 1);
    if (bytesRead == -1) {
        perror("Error reading from file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    buffer[bytesRead] = '\0';

    printf("\nFile contents:\n%s", buffer);
    printf("Total bytes read: %zd\n", bytesRead);

    // Step 4: Close the file after reading
    if (close(fd) == -1) {
        perror("Error closing file after read");
        exit(EXIT_FAILURE);
    }
    printf("File closed after reading.\n");

    // Step 5: Open file in append mode
    fd = open(FILENAME, O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("Error opening file in append mode");
        exit(EXIT_FAILURE);
    }

    // Step 6: Append data to the file
    const char *appendContent = "This line was appended using the O_APPEND flag.\n";
    bytesWritten = write(fd, appendContent, strlen(appendContent));
    if (bytesWritten == -1) {
        perror("Error appending to file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("\nAppend operation: %zd bytes written\n", bytesWritten);

    // Step 7: Close the file after appending
    if (close(fd) == -1) {
        perror("Error closing file after append");
        exit(EXIT_FAILURE);
    }
    printf("File closed after appending.\n");

    printf("\nAll file operations completed successfully.\n");
    return 0;
}
