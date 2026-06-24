/*
 * Lab 1: File Handling Using System Calls in Linux
 * Aim: Perform file I/O using Linux system calls (open, read, write, close) in C.
 * Build: gcc -Wall -Wextra -o lab1_file_handling lab1_file_handling.c
 * Run:   ./lab1_file_handling
 */

#include <stdio.h>      /* printf, perror      */
#include <stdlib.h>     /* exit, EXIT_FAILURE  */
#include <string.h>     /* strlen              */
#include <fcntl.h>      /* open, O_* flags     */
#include <unistd.h>     /* read, write, close  */

#define FILENAME    "file.txt"
#define BUFFER_SIZE 512

int main(void)
{
    int     fd;
    char    buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    ssize_t bytes_written;

    /* Step 1 & 2: open for reading, then read */
    fd = open(FILENAME, O_RDONLY);
    if (fd == -1) {
        perror("Error opening file for reading");
        exit(EXIT_FAILURE);
    }
    printf("File '%s' opened for reading (file descriptor = %d)\n", FILENAME, fd);

    bytes_read = read(fd, buffer, BUFFER_SIZE - 1);
    if (bytes_read == -1) {
        perror("Error reading from file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    buffer[bytes_read] = '\0';  /* read() does not null-terminate */

    printf("\n----- File Contents -----\n%s\n-------------------------\n", buffer);
    printf("Total bytes read: %zd\n", bytes_read);

    /* Step 3: close after reading */
    if (close(fd) == -1) {
        perror("Error closing file after reading");
        exit(EXIT_FAILURE);
    }
    printf("\nFile closed after reading.\n");

    /* Step 4: reopen in append mode (O_APPEND preserves existing data) */
    fd = open(FILENAME, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd == -1) {
        perror("Error opening file in append mode");
        exit(EXIT_FAILURE);
    }
    printf("\nFile '%s' reopened in append mode (file descriptor = %d)\n", FILENAME, fd);

    /* Step 5: append a string */
    const char *text = "This line was appended using the write() system call.\n";
    bytes_written = write(fd, text, strlen(text));
    if (bytes_written == -1) {
        perror("Error writing to file");
        close(fd);
        exit(EXIT_FAILURE);
    }
    printf("Total bytes written: %zd\n", bytes_written);

    /* Step 6: close after writing */
    if (close(fd) == -1) {
        perror("Error closing file after writing");
        exit(EXIT_FAILURE);
    }
    printf("File closed after writing.\n");

    return 0;
}
