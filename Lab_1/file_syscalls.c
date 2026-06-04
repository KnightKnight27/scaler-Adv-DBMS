#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 512

int main() {
    int fd;
    ssize_t bytes_read, bytes_written;
    char buffer[BUFFER_SIZE];
    const char *filename = "file.txt";
    const char *append_text = "\n[Appended Text]: Low-level file I/O system calls demonstration completed successfully.\n";

    printf("==================================================\n");
    printf("  Lab 1: File Handling using Linux System Calls   \n");
    printf("==================================================\n\n");

    // Pre-create the file with some initial content if it doesn't exist
    // so that the read operation has something to read.
    printf("[Setup] Checking if '%s' exists...\n", filename);
    if (access(filename, F_OK) == -1) {
        printf("[Setup] '%s' not found. Creating it with default content using system calls...\n", filename);
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Error creating default file");
            exit(EXIT_FAILURE);
        }
        const char *initial_content = "Hello Siddhant Prasad (Roll: 24BCS10255)!\nThis is the initial text in file.txt.\nWe are testing read() and write() system calls in Linux.\n";
        bytes_written = write(fd, initial_content, strlen(initial_content));
        if (bytes_written == -1) {
            perror("Error writing default content");
            close(fd);
            exit(EXIT_FAILURE);
        }
        close(fd);
        printf("[Setup] Default file created successfully.\n\n");
    }

    // 1. Open a File for Reading
    printf("Step 1: Opening '%s' in read-only mode...\n", filename);
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file for reading");
        exit(EXIT_FAILURE);
    }
    printf("Successfully opened file. File Descriptor (fd) = %d\n\n", fd);

    // 2. Read Data from the File
    printf("Step 2: Reading up to %d bytes from file...\n", BUFFER_SIZE);
    memset(buffer, 0, BUFFER_SIZE);
    bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read == -1) {
        perror("Error reading from file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("Successfully read %ld bytes.\n", (long)bytes_read);
    printf("--- File Content Content Start ---\n");
    printf("%s", buffer);
    printf("--- File Content Content End ---\n\n");

    // 3. Close the File
    printf("Step 3: Closing the file descriptor %d...\n", fd);
    if (close(fd) == -1) {
        perror("Error closing file descriptor");
        exit(EXIT_FAILURE);
    }
    printf("File descriptor closed successfully.\n\n");

    // 4. Open the File in Append Mode
    printf("Step 4: Opening '%s' in write-only append mode...\n", filename);
    fd = open(filename, O_WRONLY | O_APPEND);
    if (fd == -1) {
        perror("Error opening file for appending");
        exit(EXIT_FAILURE);
    }
    printf("Successfully opened file for appending. File Descriptor (fd) = %d\n\n", fd);

    // 5. Write/Append Data to the File
    printf("Step 5: Appending new text to the file...\n");
    bytes_written = write(fd, append_text, strlen(append_text));
    if (bytes_written == -1) {
        perror("Error appending data to file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("Successfully appended %ld bytes to '%s'.\n\n", (long)bytes_written, filename);

    // 6. Close the File Again
    printf("Step 6: Closing the file descriptor %d...\n", fd);
    if (close(fd) == -1) {
        perror("Error closing file descriptor");
        exit(EXIT_FAILURE);
    }
    printf("File descriptor closed successfully.\n\n");

    printf("==================================================\n");
    printf("  Lab 1 execution finished successfully!          \n");
    printf("==================================================\n");

    return 0;
}
