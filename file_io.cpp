#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int main() {

    // Reading
    int fd = open("file.txt", O_RDONLY);
    if (fd < 0) {
        std::cerr << "Error: " << strerror(errno) << "\n";
        return 1;
    }

    char buff[513];
    int bytes_read = read(fd, buff, 512);

    if (bytes_read < 0) {
        std::cerr << "Error: " << strerror(errno) << "\n";
        return 1;
    }

    if (bytes_read < 512)
        buff[bytes_read] = '\0';

    std::cout << bytes_read << " bytes read\n";
    std::cout << buff << "\n";

    close(fd);

    // Writing
    fd = open("file.txt", O_WRONLY | O_APPEND);
    if (fd < 0) {
        std::cerr << "Error: " << strerror(errno) << "\n";
        return 1;
    }

    char to_write[] = " My roll number is 24BCS10281\n";
    int bytes_written = write(fd, to_write, sizeof(to_write) - 1);

    if (bytes_written < 0) {
        std::cerr << "Error: " << strerror(errno) << "\n";
        return 1;
    }

    std::cout << bytes_written << " bytes written\n";

    close(fd);

    return 0;
}
