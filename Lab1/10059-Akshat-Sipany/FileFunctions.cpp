
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

bool writeDataToFile(const char* filepath, const char* payload) {
    std::cout << ">>> STARTING WRITE SEQUENCE <<<" << std::endl;

    // Open file: create if missing, write-only, truncate to 0 length.
    int fileDescriptor = open(filepath, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fileDescriptor == -1) {
        std::cerr << "Error: Unable to open file descriptor for output: " << filepath << std::endl;
        return false;
    }

    size_t payloadLength = std::strlen(payload);
    int writtenBytes = write(fileDescriptor, payload, payloadLength);
    if (writtenBytes == -1) {
        std::cerr << "Error: Write operation failed." << std::endl;
        close(fileDescriptor);
        return false;
    }

    std::cout << "Successfully saved " << writtenBytes << " bytes to: " << filepath << std::endl;
    close(fileDescriptor);
    return true;
}

bool readAndInspectFile(const char* filepath) {
    std::cout << "\n>>> STARTING READ & STAT SEQUENCE <<<" << std::endl;

    int fileDescriptor = open(filepath, O_RDONLY);
    if (fileDescriptor == -1) {
        std::cerr << "Error: Unable to open file descriptor for input: " << filepath << std::endl;
        return false;
    }

    struct stat fileStats;
    if (fstat(fileDescriptor, &fileStats) == 0) {
        std::cout << "=== File Information ===" << std::endl;
        std::cout << " Size (bytes) : " << fileStats.st_size << std::endl;
        std::cout << " Inode ID     : " << fileStats.st_ino << std::endl;
        std::cout << " Access Mask  : " << std::oct << fileStats.st_mode << std::dec << std::endl;
        std::cout << "========================" << std::endl;
    } else {
        std::cerr << "Warning: Could not retrieve file statistics." << std::endl;
    }

    // Read payload into buffer
    char readBuffer[128];
    std::memset(readBuffer, 0, sizeof(readBuffer));

    int bytesReceived = read(fileDescriptor, readBuffer, sizeof(readBuffer) - 1);
    if (bytesReceived == -1) {
        std::cerr << "Error: Read operation failed." << std::endl;
        close(fileDescriptor);
        return false;
    }

    std::cout << "Successfully retrieved " << bytesReceived << " bytes from: " << filepath << std::endl;
    std::cout << "File Content:\n" << readBuffer << std::endl;

    close(fileDescriptor);
    return true;
}

int main() {
    const char* targetPath = "system_call_test.txt";
    const char* textPayload = "Hello, Raw System Calls!\n";

    if (!writeDataToFile(targetPath, textPayload)) {
        return 1;
    }

    if (!readAndInspectFile(targetPath)) {
        return 1;
    }

    return 0;
}