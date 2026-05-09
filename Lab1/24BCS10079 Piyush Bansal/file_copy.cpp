// Lab 1 - file copy using only POSIX syscalls.
// rule was: open / read / write / close, nothing else. so no <iostream>,
// no <fstream>, no printf. just plain syscalls.
//
// I'm using "Pride and Prejudice" from Project Gutenberg as input
// (https://www.gutenberg.org/files/1342/1342-0.txt) so it's a real text
// file of decent size, not something I generated.

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *INPUT_PATH  = "input.txt";
static const char *OUTPUT_PATH = "output.txt";
static const size_t BUF_SIZE   = 4096;   // one page

// write() can return short (less bytes than asked). loop till buf is fully out.
// also retry on EINTR in case a signal interrupts the syscall.
static int write_full(int fd, const char *buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        done += (size_t)w;
    }
    return 0;
}

// helper to print a string via write() since we can't use printf
static void put_str(int fd, const char *s) { write_full(fd, s, strlen(s)); }

// convert unsigned int to ascii at the tail of a buffer.
// returns pointer to first char of the number.
static char *uint_to_ascii(unsigned long long v, char *end) {
    *--end = '\0';
    if (v == 0) { *--end = '0'; return end; }
    while (v) {
        *--end = '0' + (v % 10);
        v /= 10;
    }
    return end;
}

int main() {
    int fd_in = open(INPUT_PATH, O_RDONLY);
    if (fd_in < 0) {
        put_str(STDERR_FILENO, "open input.txt failed: ");
        put_str(STDERR_FILENO, strerror(errno));
        put_str(STDERR_FILENO, "\nmake sure input.txt is in the same dir.\n");
        return 1;
    }

    // O_TRUNC -> if output.txt already exists, wipe it first
    int fd_out = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_out < 0) {
        put_str(STDERR_FILENO, "open output.txt failed: ");
        put_str(STDERR_FILENO, strerror(errno));
        put_str(STDERR_FILENO, "\n");
        close(fd_in);
        return 1;
    }

    char buffer[BUF_SIZE];
    unsigned long long total_bytes = 0;
    unsigned long long read_calls  = 0;
    ssize_t got;

    while ((got = read(fd_in, buffer, sizeof(buffer))) > 0) {
        if (write_full(fd_out, buffer, (size_t)got) < 0) {
            put_str(STDERR_FILENO, "write failed: ");
            put_str(STDERR_FILENO, strerror(errno));
            put_str(STDERR_FILENO, "\n");
            close(fd_in); close(fd_out);
            return 1;
        }
        total_bytes += (unsigned long long)got;
        read_calls++;
    }

    if (got < 0) {
        put_str(STDERR_FILENO, "read failed: ");
        put_str(STDERR_FILENO, strerror(errno));
        put_str(STDERR_FILENO, "\n");
        close(fd_in); close(fd_out);
        return 1;
    }

    close(fd_in);
    close(fd_out);

    // print "copied X bytes in Y read() calls" without printf
    char buf1[32], buf2[32];
    char *s1 = uint_to_ascii(total_bytes, buf1 + sizeof(buf1));
    char *s2 = uint_to_ascii(read_calls,  buf2 + sizeof(buf2));
    put_str(STDOUT_FILENO, "copied ");
    put_str(STDOUT_FILENO, s1);
    put_str(STDOUT_FILENO, " bytes in ");
    put_str(STDOUT_FILENO, s2);
    put_str(STDOUT_FILENO, " read() calls\n");
    return 0;
}
