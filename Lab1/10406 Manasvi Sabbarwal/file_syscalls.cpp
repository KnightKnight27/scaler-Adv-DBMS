#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* IN_PATH  = "input.txt";
constexpr const char* OUT_PATH = "output.txt";
constexpr size_t      BUF_SIZE = 16 * 1024;

int write_all(int fd, const char* data, size_t len) {
    size_t done = 0;
    while (done < len) {
        ssize_t w = ::write(fd, data + done, len - done);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) { errno = EIO; return -1; }
        done += static_cast<size_t>(w);
    }
    return 0;
}

void emit(int fd, const char* s) {
    write_all(fd, s, std::strlen(s));
}

void report_err(const char* where) {
    emit(STDERR_FILENO, where);
    emit(STDERR_FILENO, ": ");
    emit(STDERR_FILENO, std::strerror(errno));
    emit(STDERR_FILENO, "\n");
}

char* uint_to_ascii(unsigned long long v, char* end) {
    *--end = '\0';
    if (v == 0) { *--end = '0'; return end; }
    while (v) {
        *--end = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    return end;
}

int seed_input_if_missing() {
    int fd = ::open(IN_PATH, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return (errno == EEXIST) ? 0 : -1;
    }
    static const char* lines[] = {
        "Lab 1 - raw POSIX I/O demo.\n",
        "This file was created by file_syscalls.cpp on first run.\n",
        "It is then copied to output.txt one buffer at a time using read()+write().\n",
    };
    for (const char* line : lines) {
        if (write_all(fd, line, std::strlen(line)) < 0) {
            report_err("seed write");
            ::close(fd);
            return -1;
        }
    }
    ::close(fd);
    return 0;
}

}  // namespace

int main() {
    if (seed_input_if_missing() < 0) {
        report_err("seed_input_if_missing");
        return 1;
    }

    int in_fd = ::open(IN_PATH, O_RDONLY);
    if (in_fd < 0) { report_err("open input.txt"); return 1; }

    int out_fd = ::open(OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) { report_err("open output.txt"); ::close(in_fd); return 1; }

#if defined(POSIX_FADV_SEQUENTIAL)
    ::posix_fadvise(in_fd, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    char buf[BUF_SIZE];
    unsigned long long total_bytes = 0;
    ssize_t n;

    while ((n = ::read(in_fd, buf, sizeof(buf))) > 0) {
        if (write_all(out_fd, buf, static_cast<size_t>(n)) < 0) {
            report_err("write output.txt");
            ::close(in_fd); ::close(out_fd);
            return 1;
        }
        total_bytes += static_cast<unsigned long long>(n);
    }
    if (n < 0) {
        report_err("read input.txt");
        ::close(in_fd); ::close(out_fd);
        return 1;
    }

    ::close(in_fd);
    ::close(out_fd);

    char numbuf[32];
    char* digits = uint_to_ascii(total_bytes, numbuf + sizeof(numbuf));
    emit(STDOUT_FILENO, "Copy finished. Bytes written: ");
    emit(STDOUT_FILENO, digits);
    emit(STDOUT_FILENO, "\n");
    return 0;
}
