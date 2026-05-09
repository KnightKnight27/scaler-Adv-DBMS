#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

constexpr const char *INPUT_PATH  = "input.txt";
constexpr const char *OUTPUT_PATH = "output.txt";
constexpr size_t BUFFER_SIZE = 4096;

static ssize_t writeAll(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n == 0) { errno = EIO; return -1; }
        if (n < 0)  { if (errno == EINTR) continue; return -1; }
        written += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(written);
}

static void printMsg(const char *msg) {
    writeAll(STDOUT_FILENO, msg, std::strlen(msg));
}

static void printErr(const char *ctx) {
    writeAll(STDERR_FILENO, ctx, std::strlen(ctx));
    writeAll(STDERR_FILENO, ": ", 2);
    const char *e = std::strerror(errno);
    writeAll(STDERR_FILENO, e, std::strlen(e));
    writeAll(STDERR_FILENO, "\n", 1);
}

int main() {
    // Seed input.txt if it does not exist
    int seedFd = open(INPUT_PATH, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (seedFd >= 0) {
        const char *sample =
            "Hello from raw system calls.\n"
            "This line was written using write().\n"
            "File I/O without standard library.\n";
        if (writeAll(seedFd, sample, std::strlen(sample)) < 0) {
            printErr("seed write");
            close(seedFd);
            return 1;
        }
        close(seedFd);
    } else if (errno != EEXIST) {
        printErr("create input.txt");
        return 1;
    }

    int inFd = open(INPUT_PATH, O_RDONLY);
    if (inFd < 0) { printErr("open input.txt"); return 1; }

    int outFd = open(OUTPUT_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) { printErr("open output.txt"); close(inFd); return 1; }

    char buf[BUFFER_SIZE];
    ssize_t bytesRead;
    while ((bytesRead = read(inFd, buf, sizeof(buf))) > 0) {
        if (writeAll(outFd, buf, static_cast<size_t>(bytesRead)) < 0) {
            printErr("write output.txt");
            close(inFd); close(outFd);
            return 1;
        }
    }

    if (bytesRead < 0) {
        printErr("read input.txt");
        close(inFd); close(outFd);
        return 1;
    }

    close(inFd);
    close(outFd);
    printMsg("Done. Copied input.txt -> output.txt using raw system calls.\n");
    return 0;
}
