#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <io.h>
#endif

int main() {
    const char* filename = "lab1_output.txt";
    const char* message  = "Hello from raw C++ syscalls!\n";
    int msg_len = static_cast<int>(strlen(message));

    // --- Write phase ---
    int wfd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (wfd < 0) {
        perror("open for writing failed");
        return 1;
    }
    printf("[OPEN]  fd=%d opened %s for writing\n", wfd, filename);

    ssize_t written = write(wfd, message, msg_len);
    if (written < 0) {
        perror("write failed");
        close(wfd);
        return 1;
    }
    printf("[WRITE] wrote %zd bytes to fd=%d\n", written, wfd);

#ifdef _WIN32
    _commit(wfd);
#else
    fsync(wfd);
#endif
    printf("[FSYNC] flushed fd=%d to disk\n", wfd);

    close(wfd);
    printf("[CLOSE] fd=%d closed\n", wfd);

    // --- Read phase ---
    int rfd = open(filename, O_RDONLY);
    if (rfd < 0) {
        perror("open for reading failed");
        return 1;
    }
    printf("[OPEN]  fd=%d reopened for reading\n", rfd);

    char buf[128] = {};
    ssize_t bytes_read = read(rfd, buf, sizeof(buf) - 1);
    if (bytes_read < 0) {
        perror("read failed");
        close(rfd);
        return 1;
    }
    // Strip trailing newline for display
    if (bytes_read > 0 && buf[bytes_read - 1] == '\n')
        buf[bytes_read - 1] = '\0';
    printf("[READ]  read %zd bytes: %s\n", bytes_read, buf);

    close(rfd);
    printf("[CLOSE] fd=%d closed\n", rfd);

    return 0;
}
