#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>

// Helper function to read and display the contents of a file
ssize_t read_and_display_file(const std::string& filepath) {
    // Open the file in read-only mode
    int fd = open(filepath.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to open file '" << filepath << "' for reading. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
        return -1;
    }

    // Buffer to hold read data (+1 for null terminator)
    const size_t BUFFER_SIZE = 512;
    char buffer[BUFFER_SIZE + 1];
    
    // Perform the read system call
    ssize_t bytes_read = read(fd, buffer, BUFFER_SIZE);
    if (bytes_read < 0) {
        std::cerr << "[ERROR] Failed to read from file '" << filepath << "'. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
        close(fd); // Always close the file descriptor even on failure
        return -1;
    }

    // Null-terminate the buffer to print it safely as a C-string
    buffer[bytes_read] = '\0';

    std::cout << "[SUCCESS] Read " << bytes_read << " bytes from '" << filepath << "':\n";
    std::cout << "----------------------------------------\n";
    std::cout << buffer;
    std::cout << "----------------------------------------\n";

    // Close the file descriptor and release resource
    if (close(fd) < 0) {
        std::cerr << "[WARNING] Failed to close file descriptor. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
    }

    return bytes_read;
}

// Helper function to append data to a file
ssize_t append_to_file(const std::string& filepath, const std::string& data) {
    // Open the file in write-only mode and set to append
    int fd = open(filepath.c_str(), O_WRONLY | O_APPEND);
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to open file '" << filepath << "' for writing. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
        return -1;
    }

    // Perform the write system call
    ssize_t bytes_written = write(fd, data.c_str(), data.length());
    if (bytes_written < 0) {
        std::cerr << "[ERROR] Failed to write to file '" << filepath << "'. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
        close(fd);
        return -1;
    }

    std::cout << "[SUCCESS] Appended " << bytes_written << " bytes to '" << filepath << "'.\n";

    // Close the file descriptor, flushing OS buffers to disk
    if (close(fd) < 0) {
        std::cerr << "[ERROR] Failed to close and flush file '" << filepath << "'. "
                  << "Reason: " << strerror(errno) << " (errno: " << errno << ")\n";
        return -1;
    }

    return bytes_written;
}

int main() {
    const std::string filepath = "file.txt";

    std::cout << "=== Low-Level File I/O Demonstration (Lab 1) ===\n\n";

    // Step 1: Read and display current contents
    std::cout << "Reading current file contents...\n";
    ssize_t read_status = read_and_display_file(filepath);
    if (read_status < 0) {
        return 1;
    }

    std::cout << "\n";

    // Step 2: Append new line to the file
    const std::string text_to_append = "abcd\n";
    std::cout << "Appending data to file...\n";
    ssize_t write_status = append_to_file(filepath, text_to_append);
    if (write_status < 0) {
        return 1;
    }

    std::cout << "\nFile I/O operations finished successfully.\n";
    return 0;
}
