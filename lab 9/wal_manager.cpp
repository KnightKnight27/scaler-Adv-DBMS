#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <iostream>

class WALManager {
public:
    // Constructor: Opens the log file in binary append mode
    explicit WALManager(const std::string& logPath)
        : filePath_(logPath) 
    {
        // Open file stream for writing in binary, append mode
        writeStream_.open(filePath_, std::ios::out | std::ios::binary | std::ios::app);
        if (!writeStream_.is_open()) {
            throw std::runtime_error("Error: Unable to open WAL file: " + filePath_);
        }
    }

    // Destructor: Safely flushes and closes the log stream
    ~WALManager() {
        try {
            flushLog();
            if (writeStream_.is_open()) {
                writeStream_.close();
            }
        } catch (...) {
            // Suppress exceptions in destructor to prevent program termination
        }
    }

    // Appends a new log entry to the WAL file
    void append(const std::string& entry) {
        std::lock_guard<std::mutex> guard(ioMutex_);

        // 1. Calculate record metadata
        uint32_t recordLength = static_cast<uint32_t>(entry.size());
        uint32_t entryChecksum = computeDJB2(entry);

        // 2. Write metadata header: [Length (4B)][Checksum (4B)]
        writeStream_.write(reinterpret_cast<const char*>(&recordLength), sizeof(recordLength));
        writeStream_.write(reinterpret_cast<const char*>(&entryChecksum), sizeof(entryChecksum));

        // 3. Write record payload
        writeStream_.write(entry.data(), recordLength);

        // 4. Force synchronization to disk (WAL rule: Log must reach disk before database update)
        writeStream_.flush();
    }

    // Explicit flush interface
    void flush() {
        flushLog();
    }

    // Recovers records from the log, validating checksums to prevent corruption
    std::vector<std::string> recover() {
        std::lock_guard<std::mutex> guard(ioMutex_);

        std::ifstream readStream(filePath_, std::ios::in | std::ios::binary);
        std::vector<std::string> validRecords;

        if (!readStream.is_open()) {
            // If file doesn't exist or cannot be read, return empty list
            return validRecords;
        }

        while (readStream.good()) {
            uint32_t recordLength = 0;
            uint32_t storedChecksum = 0;

            // Attempt to read the header: Length and Checksum
            if (!readStream.read(reinterpret_cast<char*>(&recordLength), sizeof(recordLength))) {
                break;
            }
            if (!readStream.read(reinterpret_cast<char*>(&storedChecksum), sizeof(storedChecksum))) {
                break; // Corrupted header or EOF
            }

            // Read the string payload
            std::string payload(recordLength, '\0');
            if (!readStream.read(&payload[0], recordLength)) {
                break; // Corrupted payload or truncated record
            }

            // Validate checksum to prevent reading corrupted records
            if (computeDJB2(payload) == storedChecksum) {
                validRecords.push_back(std::move(payload));
            } else {
                std::cerr << "Warning: Corrupted WAL record detected and skipped during recovery.\n";
            }
        }

        return validRecords;
    }

private:
    // Internal helper to flush stream to disk
    void flushLog() {
        std::lock_guard<std::mutex> guard(ioMutex_);
        if (writeStream_.is_open()) {
            writeStream_.flush();
        }
    }

    // djb2 string hashing function for simple and fast checksum validation
    static uint32_t computeDJB2(const std::string& str) {
        uint32_t hash = 5381;
        for (char c : str) {
            hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
        }
        return hash;
    }

    std::string filePath_;
    std::ofstream writeStream_;
    std::mutex ioMutex_;
};
