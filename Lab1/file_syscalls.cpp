/**
 * Name: Tuhin Samanta
 * Roll Number: 24BCS10266
 * Task 1: Raw File I/O with System Calls
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#define IN_FILE "input.txt"
#define OUT_FILE "output.txt"
#define PAGE_SIZE 4096

/**
 * Ensures all bytes are written to the file descriptor.
 * Handles partial writes and system interruptions.
 */
int safe_write(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const char *ptr = (const char *)buf;

    while (total < count) {
        ssize_t n = write(fd, ptr + total, count - total);
        
        if (n <= 0) {
            if (n < 0 && errno == EINTR) continue;
            return -1;
        }
        total += n;
    }
    return 0;
}

void log_info(const char *msg) {
    write(STDOUT_FILENO, msg, strlen(msg));
}

int main(void) {
    // Phase 1: Ensure input file exists
    int fd_init = open(IN_FILE, O_WRONLY | O_CREAT | O_EXCL, 0664);
    if (fd_init != -1) {
        const char *init_payload = "Initial data for the system call lab.\n"
                                   "Generated using low-level I/O.\n";
        if (safe_write(fd_init, init_payload, strlen(init_payload)) < 0) {
            perror("Error: failed to seed input file");
            close(fd_init);
            return EXIT_FAILURE;
        }
        close(fd_init);
    } else if (errno != EEXIST) {
        perror("Error: could not open/create input file");
        return EXIT_FAILURE;
    }

    // Phase 2: Open source and destination
    int src_fd = open(IN_FILE, O_RDONLY);
    if (src_fd < 0) {
        perror("Error: opening source");
        return EXIT_FAILURE;
    }

    int dst_fd = open(OUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (dst_fd < 0) {
        perror("Error: opening destination");
        close(src_fd);
        return EXIT_FAILURE;
    }

    // Phase 3: Transfer data via buffer
    char cache[PAGE_SIZE];
    ssize_t read_count;

    while ((read_count = read(src_fd, cache, sizeof(cache))) > 0) {
        if (safe_write(dst_fd, cache, (size_t)read_count) < 0) {
            perror("Error: write failure during copy");
            close(src_fd);
            close(dst_fd);
            return EXIT_FAILURE;
        }
    }

    if (read_count < 0) {
        perror("Error: read failure");
        close(src_fd);
        close(dst_fd);
        return EXIT_FAILURE;
    }

    // Finalize
    close(src_fd);
    close(dst_fd);

    log_info("Done. Copied input.txt to output.txt using system calls.\n");

    return EXIT_SUCCESS;
}