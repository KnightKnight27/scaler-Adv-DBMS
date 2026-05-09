#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *INPUT_FILE = "input.txt";
static const char *OUTPUT_FILE = "output.txt";
static const size_t BUFFER_SIZE = 4096;

static int write_all(int fd, const char *buffer, size_t size) {
    size_t written_total = 0;

    while (written_total < size) {
        ssize_t written = write(fd, buffer + written_total, size - written_total);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        written_total += (size_t)written;
    }

    return 0;
}

static void print_text(int fd, const char *text) {
    write_all(fd, text, strlen(text));
}

static char *number_to_text(unsigned long long value, char *end) {
    *--end = '\0';

    if (value == 0) {
        *--end = '0';
        return end;
    }

    while (value > 0) {
        *--end = '0' + (value % 10);
        value /= 10;
    }

    return end;
}

int main() {
    int input_fd = open(INPUT_FILE, O_RDONLY);

    if (input_fd < 0) {
        print_text(STDERR_FILENO, "Error: unable to open input.txt\n");
        return 1;
    }

    int output_fd = open(OUTPUT_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (output_fd < 0) {
        print_text(STDERR_FILENO, "Error: unable to create output.txt\n");
        close(input_fd);
        return 1;
    }

    char buffer[BUFFER_SIZE];
    unsigned long long total_bytes = 0;
    unsigned long long read_count = 0;

    ssize_t bytes_read;

    while ((bytes_read = read(input_fd, buffer, sizeof(buffer))) > 0) {
        if (write_all(output_fd, buffer, (size_t)bytes_read) < 0) {
            print_text(STDERR_FILENO, "Error: write failed\n");
            close(input_fd);
            close(output_fd);
            return 1;
        }

        total_bytes += (unsigned long long)bytes_read;
        read_count++;
    }

    if (bytes_read < 0) {
        print_text(STDERR_FILENO, "Error: read failed\n");
        close(input_fd);
        close(output_fd);
        return 1;
    }

    close(input_fd);
    close(output_fd);

    char bytes_buffer[32];
    char reads_buffer[32];

    char *bytes_text = number_to_text(total_bytes, bytes_buffer + sizeof(bytes_buffer));
    char *reads_text = number_to_text(read_count, reads_buffer + sizeof(reads_buffer));

    print_text(STDOUT_FILENO, "Copied ");
    print_text(STDOUT_FILENO, bytes_text);
    print_text(STDOUT_FILENO, " bytes using ");
    print_text(STDOUT_FILENO, reads_text);
    print_text(STDOUT_FILENO, " read() calls\n");

    return 0;
}
