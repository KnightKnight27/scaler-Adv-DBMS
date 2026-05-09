// Lab 1 - copy a file using only POSIX syscalls.
// no iostream, no fstream, no stdio. just open / read / write / close.
//
// input.txt is expected to exist. I downloaded "Pride and Prejudice"
// from Project Gutenberg (https://www.gutenberg.org/files/1342/1342-0.txt)
// because the assignment said real data, not made-up content.

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

static const char *INPUT  = "input.txt";
static const char *OUTPUT = "output.txt";
static const size_t BUF   = 4096;     // one page worth

// write_all: write() can return short, so loop until everything is out.
// EINTR (signal in the middle) is retried, not treated as an error.
static int write_all(int fd, const char *buf, size_t n) {
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

// tiny helper so I don't have to bring in stdio for an error message
static void say(int fd, const char *s) { write_all(fd, s, strlen(s)); }

// turn an unsigned int into ascii in-place at the end of `out`.
// returns pointer to the first character (somewhere inside `out`).
static char *u2a(unsigned long long v, char *out_end) {
    *--out_end = '\0';
    if (v == 0) { *--out_end = '0'; return out_end; }
    while (v) {
        *--out_end = '0' + (v % 10);
        v /= 10;
    }
    return out_end;
}

int main() {
    int in_fd = open(INPUT, O_RDONLY);
    if (in_fd < 0) {
        say(STDERR_FILENO, "open input.txt: ");
        say(STDERR_FILENO, strerror(errno));
        say(STDERR_FILENO, "\n(download a text file as input.txt first)\n");
        return 1;
    }

    // O_TRUNC so re-running doesn't append the old output to itself.
    int out_fd = open(OUTPUT, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        say(STDERR_FILENO, "open output.txt: ");
        say(STDERR_FILENO, strerror(errno));
        say(STDERR_FILENO, "\n");
        close(in_fd);
        return 1;
    }

    char buf[BUF];
    unsigned long long total = 0;
    unsigned long long reads = 0;
    ssize_t n;
    while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
        if (write_all(out_fd, buf, (size_t)n) < 0) {
            say(STDERR_FILENO, "write: ");
            say(STDERR_FILENO, strerror(errno));
            say(STDERR_FILENO, "\n");
            close(in_fd); close(out_fd);
            return 1;
        }
        total += (unsigned long long)n;
        reads++;
    }
    if (n < 0) {
        say(STDERR_FILENO, "read: ");
        say(STDERR_FILENO, strerror(errno));
        say(STDERR_FILENO, "\n");
        close(in_fd); close(out_fd);
        return 1;
    }

    close(in_fd);
    close(out_fd);

    // print "copied N bytes in M reads\n" without using printf
    char num1[32], num2[32];
    char *p1 = u2a(total, num1 + sizeof(num1));
    char *p2 = u2a(reads, num2 + sizeof(num2));
    say(STDOUT_FILENO, "copied ");
    say(STDOUT_FILENO, p1);
    say(STDOUT_FILENO, " bytes in ");
    say(STDOUT_FILENO, p2);
    say(STDOUT_FILENO, " read() calls\n");
    return 0;
}
