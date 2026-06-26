#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

int main()
{
    // Opened the file in read-only mode to begin the experiment
    int fd = open("file.txt", O_RDONLY);
    if (fd < 0)
    {
        std::cerr << "Error opening file for reading: " << std::strerror(errno) << std::endl;
        return 1;
    }

    // Allocated 513 bytes to leave room for the null-terminator safely
    char buff[513];
    ssize_t bytes_read = read(fd, buff, 512);

    if (bytes_read < 0)
    {
        std::cerr << "Error reading file: " << std::strerror(errno) << std::endl;
        close(fd);
        return 1;
    }

    // Explicitly added a null terminator so cout handles it as a valid string
    buff[bytes_read] = '\0';

    std::cout << "----- File Content Read -----\n"
              << buff << "\n----- End of the file content-----\n";
    std::cout << "Total bytes read: " << bytes_read << " bytes.\n\n";

    close(fd);

    // I will now reopen the same file, using flags to write and append data
    fd = open("file.txt", O_WRONLY | O_APPEND);
    if (fd < 0)
    {
        std::cerr << "Error opening file for appending: " << std::strerror(errno) << std::endl;
        return 1;
    }

    std::string to_write = "\nAppending this new line using system calls!\n";
    ssize_t bytes_written = write(fd, to_write.c_str(), to_write.length());

    if (bytes_written < 0)
    {
        std::cerr << "Error writing to file: " << std::strerror(errno) << std::endl;
        close(fd);
        return 1;
    }

    std::cout << "Total bytes written: " << bytes_written << " bytes successfully appended." << std::endl;

    // We make sure to close the file descriptor a second time to prevent resource leaks
    close(fd);

    return 0;
}