
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
    const char* filePath = "system_call_test.txt";
    const char* payload = "Hello, Raw System Calls!\n";
    
    std::cout << "=== Writing to file ===" << std::endl;
    
    // Open file: create if it doesn't exist, write-only, truncate existing content
    int fileDescriptor = open(filePath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);

    if (fileDescriptor == -1) {
        std::cerr << "Error: Unable to open " << filePath << " for writing." << std::endl;
        return 1;
    }

    // Write payload content to file
    int writtenBytes = write(fileDescriptor, payload, std::strlen(payload));

    if (writtenBytes == -1) {
        std::cerr << "Error: Write operation failed." << std::endl;
        close(fileDescriptor);
        return 1;
    }
    
    std::cout << "Successfully wrote " << writtenBytes << " bytes to " << filePath << std::endl;

    close(fileDescriptor);

    std::cout << "\n=== Reading from file ===" << std::endl;
    
    // Reopen in read-only mode
    fileDescriptor = open(filePath, O_RDONLY);

    if (fileDescriptor == -1) {
        std::cerr << "Error: Unable to open " << filePath << " for reading." << std::endl;
        return 1;
    }

    // Query inode metadata and display details
    struct stat fileStats;
    if (fstat(fileDescriptor, &fileStats) == 0) {
        std::cout << "Size (bytes)  : " << fileStats.st_size << std::endl;
        std::cout << "Inode         : " << fileStats.st_ino << std::endl;
        std::cout << "Mode (octal)  : " << std::oct << fileStats.st_mode << std::dec << std::endl;
    } else {
        std::cerr << "Warning: Could not retrieve file statistics." << std::endl;
    }

    // Read the contents of the file back
    char inputBuffer[128];
    std::memset(inputBuffer, 0, sizeof(inputBuffer));

    int readBytes = read(fileDescriptor, inputBuffer, sizeof(inputBuffer) - 1);

    if (readBytes == -1) {
        std::cerr << "Error: Read operation failed." << std::endl;
        close(fileDescriptor);
        return 1;
    }

    std::cout << "Loaded " << readBytes << " bytes from " << filePath << std::endl;
    std::cout << "Payload:\n" << inputBuffer << std::endl;

    close(fileDescriptor);

    return 0;
}