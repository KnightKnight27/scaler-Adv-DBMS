#include <iostream>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

/**
 * Advanced DBMS Lab 1: Storage Stack & OS Execution Journey
 * Refactored modular implementation using direct POSIX system calls.
 */

// Global configuration matching exact lab output requirements
constexpr const char* TARGET_FILENAME = "assignment-data.txt";
constexpr std::string_view PAYLOAD_DATA = 
    "Advanced DBMS Lab 1\n"
    "This file was written using POSIX open/write and read using open/read.\n"
    "It demonstrates how user code reaches the operating system through system calls.\n";

// Helper function to handle the write path and physical durability
bool persist_payload_to_disk(const char* filepath, std::string_view data) {
    // Open file: Write-Only, Create if missing, Overwrite/Truncate if existing, Mode 0666
    int target_fd = open(filepath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (target_fd == -1) {
        perror("System Call Error: open() failed during write phase");
        return false;
    }

    // Dispatch bytes to the kernel's page cache
    ssize_t written_bytes = write(target_fd, data.data(), data.length());
    if (written_bytes < 0 || static_cast<size_t>(written_bytes) != data.length()) {
        perror("System Call Error: write() failed or performed a partial write");
        close(target_fd);
        return false;
    }

    // Explicitly enforce physical durability by flushing dirty pages from RAM to disk
    if (fsync(target_fd) == -1) {
        perror("System Call Error: fsync() failed to verify hardware persistence");
        close(target_fd);
        return false;
    }

    // Release target file handle
    if (close(target_fd) == -1) {
        perror("System Call Error: close() failed during write phase");
        return false;
    }

    return true;
}

// Helper function to handle the read path from kernel cache/storage
bool fetch_payload_from_disk(const char* filepath) {
    // Open file in Read-Only mode
    int source_fd = open(filepath, O_RDONLY);
    if (source_fd == -1) {
        perror("System Call Error: open() failed during read phase");
        return false;
    }

    // Allocate a buffer slightly larger than expected payload to ensure null-termination
    std::vector<char> read_buffer(2048, '\0');
    ssize_t bytes_retrieved = read(source_fd, read_buffer.data(), read_buffer.size() - 1);

    if (bytes_retrieved < 0) {
        perror("System Call Error: read() failed to retrieve file blocks");
        close(source_fd);
        return false;
    }

    // Safely close target file handle
    close(source_fd);

    // Print output to match lab verification specifications exactly
    std::cout << "Read back from file:\n\n";
    std::cout << read_buffer.data() << std::endl;

    return true;
}

int main() {
    // Phase 1: Execute Kernel Write & Sync
    if (!persist_payload_to_disk(TARGET_FILENAME, PAYLOAD_DATA)) {
        std::cerr << "Fatal: Failed to complete the persistent write pipeline.\n";
        return EXIT_FAILURE;
    }

    std::cout << "File written successfully: " << TARGET_FILENAME << "\n\n";

    // Phase 2: Execute Kernel Read
    if (!fetch_payload_from_disk(TARGET_FILENAME)) {
        std::cerr << "Fatal: Failed to complete the data retrieval pipeline.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}