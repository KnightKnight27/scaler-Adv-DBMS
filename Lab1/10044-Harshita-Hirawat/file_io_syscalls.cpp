#include <cstddef>

#if !defined(__linux__) || !defined(__x86_64__)
#error "This example requires Linux on x86-64."
#endif

#include <fcntl.h>
#include <sys/syscall.h>

static constexpr int STDOUT_FD = 1;
static constexpr int STDERR_FD = 2;

// Linux x86-64 raw syscall helper.
// Arguments follow the Linux syscall ABI:
// rax = syscall number, rdi/rsi/rdx/r10/r8/r9 = up to six arguments.
static long raw_syscall6(long number,long arg1 = 0, long arg2 = 0, long arg3 = 0, long arg4 = 0, long arg5 = 0, long arg6 = 0) {
    long result;

    register long r10 __asm__("r10") = arg4;
    register long r8 __asm__("r8") = arg5;
    register long r9 __asm__("r9") = arg6;

    __asm__ volatile("syscall" : "=a"(result) : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");

    return result;
}

static std::size_t string_length(const char* text) {
    std::size_t length = 0;
    while(text[length] != '\0') {
        ++length;
    }
    return length;
}

static void write_all(int fd, const char* data, std::size_t size) {
    std::size_t written = 0;

    while(written < size) {
        long result = raw_syscall6(SYS_write, fd, reinterpret_cast<long>(data + written), static_cast<long>(size - written));

        if(result <= 0) {
            raw_syscall6(SYS_write, STDERR_FD, reinterpret_cast<long>("write syscall failed\n"), 21);
            raw_syscall6(SYS_exit, 1);
        }

        written += static_cast<std::size_t>(result);
    }
}

static void write_text(int fd, const char* text) {
    write_all(fd, text, string_length(text));
}

static void write_number(int fd, long value) {
    char buffer[32];
    int index = 0;

    if(value == 0) {
        write_all(fd, "0", 1);
        return;
    }

    if(value < 0) {
        write_all(fd, "-", 1);
        value = -value;
    }

    while(value > 0 && index < static_cast<int>(sizeof(buffer))) {
        buffer[index++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    }

    while(index > 0) {
        --index;
        write_all(fd, &buffer[index], 1);
    }
}

static void fail_and_exit(const char* message) {
    write_text(STDERR_FD, message);
    write_text(STDERR_FD, "\n");
    raw_syscall6(SYS_exit, 1);
}

int main() {
    const char* filename = "example.txt";
    const char* content =
        "Hello from C++ using raw Linux system calls.\n"
        "This file was opened, written, closed, opened again, and read back.\n";

    // openat is the modern Linux system call used by glibc open().
    long write_fd = raw_syscall6(SYS_openat, AT_FDCWD, reinterpret_cast<long>(filename), O_CREAT | O_WRONLY | O_TRUNC, 0644);

    if(write_fd < 0) {
        fail_and_exit("openat syscall failed while creating the file");
    }

    write_all(static_cast<int>(write_fd), content, string_length(content));

    long close_result = raw_syscall6(SYS_close, write_fd);
    if(close_result < 0) {
        fail_and_exit("close syscall failed after writing");
    }

    long read_fd = raw_syscall6(SYS_openat, AT_FDCWD, reinterpret_cast<long>(filename), O_RDONLY, 0);

    if(read_fd < 0) {
        fail_and_exit("openat syscall failed while opening the file for reading");
    }

    write_text(STDOUT_FD, "File descriptor used for reading: ");
    write_number(STDOUT_FD, read_fd);
    write_text(STDOUT_FD, "\n\nData read from file:\n");

    char buffer[64];
    while(true) {
        long bytes_read = raw_syscall6(SYS_read, read_fd, reinterpret_cast<long>(buffer), sizeof(buffer));

        if(bytes_read < 0) {
            fail_and_exit("read syscall failed");
        }

        if(bytes_read == 0) {
            break;
        }

        write_all(STDOUT_FD, buffer, static_cast<std::size_t>(bytes_read));
    }

    close_result = raw_syscall6(SYS_close, read_fd);
    if(close_result < 0) {
        fail_and_exit("close syscall failed after reading");
    }

    write_text(STDOUT_FD, "\nCompleted open/write/read/close using raw syscalls.\n");
    return 0;
}
