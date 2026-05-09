#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

// these are just thin wrappers so we do not call syscall() inline everywhere

static long raw_open(const char *path, int flags, int mode) {
    return syscall(SYS_open, path, flags, mode);
}

static long raw_write(int fd, const void *buf, unsigned long count) {
    return syscall(SYS_write, fd, buf, count);
}

static long raw_read(int fd, void *buf, unsigned long count) {
    return syscall(SYS_read, fd, buf, count);
}

static long raw_close(int fd) {
    return syscall(SYS_close, fd);
}

// no printf means we print the hard way
static void print(const char *s) {
    unsigned long len = 0;
    while (s[len]) ++len;
    raw_write(1, s, len);
}

static void print_num(long n) {
    char buf[32];
    int neg = (n < 0);
    if (neg) n = -n;
    int i = 31;
    buf[i] = 0;
    if (n == 0) { buf[--i] = '0'; }
    while (n > 0) { buf[--i] = '0' + (char)(n % 10); n /= 10; }
    if (neg) buf[--i] = '-';
    print(buf + i);
}

int main() {
    const char *path = "/tmp/syscall_io_demo.txt";

    // kernel walks the path, creates the file if it does not exist,
    // and gives us back a number (fd) that represents "this open file"
    long fd_w = raw_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_w < 0) {
        print("open failed, errno="); print_num(errno); print("\n");
        return 1;
    }
    print("write fd="); print_num(fd_w); print("\n");

    const char *message =
        "Hello from raw syscalls!\n"
        "No libc. No streams. Just syscall() to the kernel.\n";

    unsigned long msg_len = 0;
    while (message[msg_len]) ++msg_len;

    // data goes into the kernel page cache here, not on disk yet
    // a background thread handles flushing to disk later
    long written = raw_write((int)fd_w, message, msg_len);
    if (written < 0) {
        print("write failed, errno="); print_num(errno); print("\n");
        raw_close((int)fd_w);
        return 1;
    }
    print("bytes written="); print_num(written); print("\n");

    // dropping the fd, kernel will eventually flush the dirty pages to disk
    raw_close((int)fd_w);

    // open the same file again, this time for reading
    long fd_r = raw_open(path, O_RDONLY, 0);
    if (fd_r < 0) {
        print("open failed, errno="); print_num(errno); print("\n");
        return 1;
    }
    print("read fd="); print_num(fd_r); print("\n");

    char buf[512] = {};

    // since we just wrote this, the page is still in cache
    // kernel copies it straight to our buffer, no disk read needed
    long got = raw_read((int)fd_r, buf, sizeof(buf) - 1);
    if (got < 0) {
        print("read failed, errno="); print_num(errno); print("\n");
        raw_close((int)fd_r);
        return 1;
    }
    raw_close((int)fd_r);

    print("\n--- file contents ---\n");
    raw_write(1, buf, (unsigned long)got);
    print("---------------------\n");

    return 0;
}