#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char *SRC_FILE  = "input.txt";
static const char *DEST_FILE = "output.txt";
static const int   CHUNK     = 8192;

static int safeWrite(int fd, const void *data, size_t total) {
    const char *ptr = static_cast<const char *>(data);
    size_t remaining = total;
    while (remaining > 0) {
        ssize_t ret = write(fd, ptr, remaining);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        ptr       += ret;
        remaining -= static_cast<size_t>(ret);
    }
    return 0;
}

static void log_msg(const char *msg) {
    safeWrite(STDOUT_FILENO, msg, std::strlen(msg));
}

static void log_err(const char *label) {
    const char *err = std::strerror(errno);
    safeWrite(STDERR_FILENO, label, std::strlen(label));
    safeWrite(STDERR_FILENO, " failed: ", 9);
    safeWrite(STDERR_FILENO, err, std::strlen(err));
    safeWrite(STDERR_FILENO, "\n", 1);
}

static int seed_input() {
    int fd = open(SRC_FILE, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return (errno == EEXIST) ? 0 : -1;

    const char *lines[] = {
        "System call lab - Mohit Kumar\n",
        "Using open/read/write/close directly.\n",
        "No stdio, no fstream — just the kernel.\n",
        "Each read() may return fewer bytes than requested.\n",
        "Always loop until the full buffer is written.\n",
    };
    for (const char *line : lines) {
        if (safeWrite(fd, line, std::strlen(line)) < 0) {
            log_err("seed write");
            close(fd);
            return -1;
        }
    }
    close(fd);
    return 0;
}

int main() {
    if (seed_input() < 0) return 1;

    int src = open(SRC_FILE, O_RDONLY);
    if (src < 0) { log_err("open src"); return 1; }

    int dst = open(DEST_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst < 0) { log_err("open dst"); close(src); return 1; }

    char chunk[CHUNK];
    ssize_t n;
    long long total = 0;

    while ((n = read(src, chunk, sizeof(chunk))) > 0) {
        if (safeWrite(dst, chunk, static_cast<size_t>(n)) < 0) {
            log_err("write dst");
            close(src); close(dst);
            return 1;
        }
        total += n;
    }

    if (n < 0) { log_err("read src"); close(src); close(dst); return 1; }

    close(src);
    close(dst);

    char msg[] = "Copy complete.\n";
    log_msg(msg);
    return 0;
}
