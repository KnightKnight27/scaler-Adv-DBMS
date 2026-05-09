#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr const char* IN_PATH = "input.txt";
constexpr const char* OUT_PATH = "output.txt";
constexpr size_t BUF_SIZE = 16 * 1024;
constexpr mode_t OUTPUT_FILE_MODE = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
constexpr int EXIT_OK = 0;
constexpr int EXIT_IO_ERROR = 1;

[[nodiscard]] bool write_all(int fd, const char* data, size_t len) {
    size_t written = 0;

    while (written < len) {
        size_t to_write = len - written;
        if (to_write > static_cast<size_t>(SSIZE_MAX)) {
            to_write = static_cast<size_t>(SSIZE_MAX);
        }

        const ssize_t n = ::write(fd, data + written, to_write);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return false;
        }

        if (n == 0) {
            errno = EIO;
            return false;
        }

        written += static_cast<size_t>(n);
    }

    return true;
}

[[nodiscard]] bool emit(int fd, const char* text) {
    return write_all(fd, text, std::strlen(text));
}

[[nodiscard]] bool report_error(const char* where, int error_number) {
    bool ok = true;

    if (!emit(STDERR_FILENO, where)) {
        ok = false;
    }
    if (!emit(STDERR_FILENO, ": ")) {
        ok = false;
    }
    if (!emit(STDERR_FILENO, std::strerror(error_number))) {
        ok = false;
    }
    if (!emit(STDERR_FILENO, "\n")) {
        ok = false;
    }

    return ok;
}

[[nodiscard]] bool report_errno(const char* where) {
    const int error_number = errno;
    return report_error(where, error_number);
}

class FileDescriptor {
public:
    FileDescriptor() = default;
    explicit FileDescriptor(int fd) : fd_(fd) {}

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    ~FileDescriptor() {
        if (fd_ >= 0) {
            (void)::close(fd_);
        }
    }

    [[nodiscard]] int get() const {
        return fd_;
    }

    [[nodiscard]] bool valid() const {
        return fd_ >= 0;
    }

    [[nodiscard]] bool close(const char* context) {
        if (!valid()) {
            return true;
        }

        // Do not retry close() after EINTR: POSIX leaves the descriptor state
        // unspecified, and retrying can accidentally close a reused fd.
        const int fd = fd_;
        fd_ = -1;

        if (::close(fd) == 0) {
            return true;
        }

        return report_errno(context);
    }

private:
    int fd_ = -1;
};

char* uint_to_ascii(unsigned long long v, char* end) {
    *--end = '\0';
    if (v == 0) {
        *--end = '0';
        return end;
    }

    while (v) {
        *--end = static_cast<char>('0' + (v % 10));
        v /= 10;
    }

    return end;
}

[[nodiscard]] bool advise_sequential_reads(const FileDescriptor& fd) {
#if defined(POSIX_FADV_SEQUENTIAL)
    const int rc = ::posix_fadvise(fd.get(), 0, 0, POSIX_FADV_SEQUENTIAL);
    if (rc != 0) {
        return report_error("posix_fadvise input.txt", rc);
    }
#else
    (void)fd;
#endif

    return true;
}

[[nodiscard]] bool copy_contents(
    const FileDescriptor& in_fd,
    const FileDescriptor& out_fd,
    unsigned long long* total_bytes) {
    char buffer[BUF_SIZE];

    for (;;) {
        const ssize_t n = ::read(in_fd.get(), buffer, sizeof(buffer));

        if (n > 0) {
            if (!write_all(out_fd.get(), buffer, static_cast<size_t>(n))) {
                (void)report_errno("write output.txt");
                return false;
            }

            *total_bytes += static_cast<unsigned long long>(n);
            continue;
        }

        if (n == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        (void)report_errno("read input.txt");
        return false;
    }
}

[[nodiscard]] bool emit_copy_summary(unsigned long long total_bytes) {
    char numbuf[32];
    char* digits = uint_to_ascii(total_bytes, numbuf + sizeof(numbuf));

    bool ok = true;

    if (!emit(STDOUT_FILENO, "Copy finished. Bytes written: ")) {
        ok = false;
    }
    if (!emit(STDOUT_FILENO, digits)) {
        ok = false;
    }
    if (!emit(STDOUT_FILENO, "\n")) {
        ok = false;
    }

    return ok;
}

}  // namespace

int main() {
    FileDescriptor in_fd(::open(IN_PATH, O_RDONLY));
    if (!in_fd.valid()) {
        (void)report_errno("open input.txt");
        return EXIT_IO_ERROR;
    }

    FileDescriptor out_fd(::open(
        OUT_PATH,
        O_WRONLY | O_CREAT | O_TRUNC,
        OUTPUT_FILE_MODE));

    if (!out_fd.valid()) {
        (void)report_errno("open output.txt");
        return EXIT_IO_ERROR;
    }

    if (!advise_sequential_reads(in_fd)) {
        return EXIT_IO_ERROR;
    }

    unsigned long long total_bytes = 0;

    if (!copy_contents(in_fd, out_fd, &total_bytes)) {
        return EXIT_IO_ERROR;
    }

    bool close_ok = true;
    close_ok = out_fd.close("close output.txt") && close_ok;
    close_ok = in_fd.close("close input.txt") && close_ok;

    if (!close_ok) {
        return EXIT_IO_ERROR;
    }

    if (!emit_copy_summary(total_bytes)) {
        (void)report_errno("write stdout");
        return EXIT_IO_ERROR;
    }

    return EXIT_OK;
}
