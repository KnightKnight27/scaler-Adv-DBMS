#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

size_t c_string_length(const char *text) {
    size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

bool write_all(int fd, const char *data, size_t length) {
    while (length > 0) {
        const ssize_t written = ::write(fd, data, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += written;
        length -= static_cast<size_t>(written);
    }
    return true;
}

void write_text(int fd, const char *text) {
    write_all(fd, text, c_string_length(text));
}

void write_unsigned(int fd, unsigned long long value) {
    char buffer[32];
    size_t index = 0;

    do {
        buffer[index++] = static_cast<char>('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value != 0ULL);

    while (index > 0) {
        char digit = buffer[--index];
        write_all(fd, &digit, 1);
    }
}

void print_file_info(int fd, const char *label, const struct stat &file_stat) {
    write_text(fd, label);
    write_text(fd, " inode=");
    write_unsigned(fd, static_cast<unsigned long long>(file_stat.st_ino));
    write_text(fd, " size=");
    write_unsigned(fd, static_cast<unsigned long long>(file_stat.st_size));
    write_text(fd, " bytes\n");
}

}  // namespace

int main(int argc, char *argv[]) {
    if (argc != 3) {
        write_text(STDERR_FILENO, "Usage: syscall_file_demo <input-file> <output-file>\n");
        return 1;
    }

    const char *input_path = argv[1];
    const char *output_path = argv[2];

    const int input_fd = ::open(input_path, O_RDONLY);
    if (input_fd < 0) {
        write_text(STDERR_FILENO, "Failed to open input file\n");
        return 2;
    }

    struct stat input_stat {};
    if (::fstat(input_fd, &input_stat) == 0) {
        print_file_info(STDOUT_FILENO, "Input", input_stat);
    }

    const int output_fd = ::open(output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        write_text(STDERR_FILENO, "Failed to open output file\n");
        ::close(input_fd);
        return 3;
    }

    char buffer[4096];
    while (true) {
        ssize_t bytes_read = ::read(input_fd, buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            write_text(STDERR_FILENO, "Read failed\n");
            ::close(input_fd);
            ::close(output_fd);
            return 4;
        }

        if (bytes_read == 0) {
            break;
        }

        if (!write_all(output_fd, buffer, static_cast<size_t>(bytes_read))) {
            write_text(STDERR_FILENO, "Write failed\n");
            ::close(input_fd);
            ::close(output_fd);
            return 5;
        }
    }

    ::close(input_fd);
    ::close(output_fd);

    write_text(STDOUT_FILENO, "File copied successfully using low-level system calls.\n");
    return 0;
}