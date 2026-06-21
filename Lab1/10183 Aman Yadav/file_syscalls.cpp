#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* kInPath  = "input.txt";
constexpr const char* kOutPath = "output.txt";
constexpr size_t      kBuf     = 16 * 1024;

int write_full(int fd, const char* data, size_t len) {
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

void say(int fd, const char* s) {
    write_full(fd, s, std::strlen(s));
}

void perr(const char* where) {
    say(STDERR_FILENO, where);
    say(STDERR_FILENO, ": ");
    say(STDERR_FILENO, std::strerror(errno));
    say(STDERR_FILENO, "\n");
}

char* utoa(unsigned long long v, char* end) {
    *--end = '\0';
    if (v == 0) { *--end = '0'; return end; }
    while (v) {
        *--end = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    return end;
}

int ensure_input() {
    int fd = ::open(kInPath, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        return (errno == EEXIST) ? 0 : -1;
    }
    static const char* seed[] = {
        "Hello, this is a sample input file.\n",
        "It is copied to output.txt using raw system calls.\n",
    };
    for (const char* line : seed) {
        if (write_full(fd, line, std::strlen(line)) < 0) {
            perr("seed write");
            ::close(fd);
            return -1;
        }
    }
    ::close(fd);
    return 0;
}

}  // namespace

int main() {
    if (ensure_input() < 0) {
        perr("ensure_input");
        return 1;
    }

    int in = ::open(kInPath, O_RDONLY);
    if (in < 0) { perr("open input.txt"); return 1; }

    int out = ::open(kOutPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perr("open output.txt"); ::close(in); return 1; }

#if defined(POSIX_FADV_SEQUENTIAL)
    ::posix_fadvise(in, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif

    char buf[kBuf];
    unsigned long long total = 0;
    ssize_t n;

    while ((n = ::read(in, buf, sizeof(buf))) > 0) {
        if (write_full(out, buf, static_cast<size_t>(n)) < 0) {
            perr("write output.txt");
            ::close(in); ::close(out);
            return 1;
        }
        total += static_cast<unsigned long long>(n);
    }
    if (n < 0) {
        perr("read input.txt");
        ::close(in); ::close(out);
        return 1;
    }

    ::close(in);
    ::close(out);

    char num[32];
    char* p = utoa(total, num + sizeof(num));
    say(STDOUT_FILENO, "Copy finished. Bytes written: ");
    say(STDOUT_FILENO, p);
    say(STDOUT_FILENO, "\n");
    return 0;
}
