
#include <iostream>
#include <fcntl.h>      
#include <sys/types.h>
#include <sys/stat.h>   
#include <cstring>    

#ifdef _WIN32
    #include <io.h>
    #define open _open
    #define read _read
    #define write _write
    #define close _close
    #define O_CREAT _O_CREAT
    #define O_WRONLY _O_WRONLY
    #define O_RDONLY _O_RDONLY
    #define O_TRUNC _O_TRUNC
#else
    #include <unistd.h>
#endif

int main() {
    const char* filename = "system_call_test.txt";
    const char* data = "Hello, Raw System Calls!\n";
    
    std::cout << "--- WRITING TO FILE ---" << std::endl;
    
    // opening the file 
    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);

    if (fd == -1) {
        std::cerr << "Error: Failed to open file for writing." << std::endl;
        return 1;
    }

    // writing to the file    
    int bytes_written = write(fd, data, std::strlen(data));

    if (bytes_written == -1) {
        std::cerr << "Error: Failed to write to file." << std::endl;
        close(fd);
        return 1;
    }
    
    std::cout << "Wrote " << bytes_written << " bytes to " << filename << std::endl;

    // closing the file
    close(fd);

    std::cout << "\n--- READING FROM FILE ---" << std::endl;
    
    // opening the file for reading
    fd = open(filename, O_RDONLY);

    if (fd == -1) {
        std::cerr << "Error: Failed to open file for reading." << std::endl;
        return 1;
    }

    // fetching inode metadata via fstat
    struct stat file_info;
    if (fstat(fd, &file_info) == 0) {
        std::cout << "File size   : " << file_info.st_size << " bytes" << std::endl;
        std::cout << "Inode number: " << file_info.st_ino << std::endl;
        std::cout << "Permissions : " << std::oct << file_info.st_mode << std::dec << std::endl;
    } else {
        std::cerr << "Warning: fstat failed." << std::endl;
    }

    // reading from the file
    char buffer[128];
    std::memset(buffer, 0, sizeof(buffer));

    int bytes_read = read(fd, buffer, sizeof(buffer) - 1);

    if (bytes_read == -1) {
        std::cerr << "Error: Failed to read from file." << std::endl;
        close(fd);
        return 1;
    }

    std::cout << "Read " << bytes_read << " bytes from " << filename << std::endl;
    std::cout << "Data content:\n" << buffer << std::endl;

    // closing the file
    close(fd);

    return 0;
}
