#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>

int main() {

    const char *filename = "demo.txt";

    const char *message =
        "Hello from raw system calls!\n";

    // OPEN FILE
    int fd = syscall(
        SYS_open,
        filename,
        O_CREAT | O_WRONLY | O_TRUNC,
        0644
    );

    if (fd < 0) {
        return 1;
    }

    // WRITE FILE
    syscall(
        SYS_write,
        fd,
        message,
        30
    );

    // RESET OFFSET
    syscall(
        SYS_close,
        fd
    );

    // OPEN AGAIN FOR READING
    fd = syscall(
        SYS_open,
        filename,
        O_RDONLY
    );

    if (fd < 0) {
        return 1;
    }

    char buffer[100];

    long bytesRead = syscall(
        SYS_read,
        fd,
        buffer,
        sizeof(buffer)
    );

    // WRITE TO TERMINAL
    syscall(
        SYS_write,
        1,
        buffer,
        bytesRead
    );

    syscall(
        SYS_close,
        fd
    );

    return 0;
}