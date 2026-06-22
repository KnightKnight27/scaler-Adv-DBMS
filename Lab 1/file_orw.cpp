#include <iostream>
#include <fcntl.h>

// ssize_t is available in <sys/types.h> in mac and linux
// so this makes it work for windows by using a windows equivalent type i.e. SSIZE_T
#ifdef _WIN32
    #include <BaseTsd.h>
    typedef SSIZE_T ssize_t;
#else
    #include <sys/types.h>
#endif

// Because unistd works for mac os and linux but not for windows, we use a wrapper for this
#ifdef _WIN32
    #include <io.h>
    #include <process.h>
#else
    #include <unistd.h>
#endif

#include <cstring>

int main() {
    const char* file = "output.txt";
    const char* message = "Hello!\n";
    const int buffer_size = 256;

    
    // Open and Write
    int fd = open(file, O_RDWR | O_CREAT | O_TRUNC, 0644);

    if(fd == -1) {
        std::cerr << "Error: Failed to open file for writing.\n";
        return 1;
    }

    std::cout << "Info: File opened successfully. File Descriptor = " << fd << "\n";

    // Write
    ssize_t bytes_to_write = static_cast<ssize_t>(strlen(message));
    ssize_t bytes_written = write(fd, message, bytes_to_write);

    if(bytes_written == -1) {
        std::cerr << "Error: Failed to write.\n";
        close(fd);
        return 1;
    }

    if(bytes_to_write > bytes_written) {
        std::cerr << "Warning: Partial write. Only " << bytes_written << " out of " << bytes_to_write <<" bytes written.\n";
    }

    std::cout << "Info: Wrote " << bytes_written <<" bytes to the file.\n";

    // lSeek()
    off_t new_offset = lseek(fd, 0, SEEK_SET);

    if (new_offset == static_cast<off_t>(-1)) {
        std::cerr << "Error: lseek() failed.\n";
        close(fd);
        return 1;
    }

    std::cout << "Info: Seek pointer reset to offset " << new_offset << ".\n";

    // Open and Read
    char buffer[buffer_size] = {0};
    ssize_t bytes_read = read(fd, buffer, buffer_size - 1);

    if(bytes_read == -1) {
        std::cerr << "Error: Failed to read file.\n";
        close(fd);
        return 1;
    }
    std::cout << "Info: Successfully read " << bytes_read << " bytes.\n";

    std::cout << "\n---- File Content ----\n";
    std::cout << buffer;
    std::cout << "----------------------\n\n";

    if(close(fd) == -1) {
        std::cerr << "Error: Failed to close file.\n";
        return 1;
    }

    std::cout << "Info: File Descriptor " << fd << " closed successfully.\n";
    std::cout << "Completed.\n";

    return 0;
}