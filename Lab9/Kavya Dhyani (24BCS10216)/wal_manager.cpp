#pragma once

#include <fstream>
#include <string>
#include <mutex>
#include <vector>

class WALManager {
public:
    explicit WALManager(const std::string& logFile)
        : logFile_(logFile)
    {
        logStream_.open(logFile_,
                        std::ios::app |
                        std::ios::out |
                        std::ios::binary);

        if (!logStream_.is_open()) {
            throw std::runtime_error("Failed to open WAL file");
        }
    }

    ~WALManager() {
        flush();
        logStream_.close();
    }

    // Append a log record before applying data changes
    void append(const std::string& record) {
        std::lock_guard<std::mutex> lock(mutex_);

        uint32_t len = static_cast<uint32_t>(record.size());

        logStream_.write(
            reinterpret_cast<const char*>(&len),
            sizeof(len));

        logStream_.write(
            record.data(),
            record.size());

        // WAL rule:
        // log must reach disk before data page update
        logStream_.flush();
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        logStream_.flush();
    }

    // Recovery API
    std::vector<std::string> recover() {
        std::lock_guard<std::mutex> lock(mutex_);

        std::ifstream in(
            logFile_,
            std::ios::binary);

        std::vector<std::string> records;

        while (true) {
            uint32_t len;

            if (!in.read(
                    reinterpret_cast<char*>(&len),
                    sizeof(len))) {
                break;
            }

            std::string record(len, '\0');

            if (!in.read(record.data(), len)) {
                break;
            }

            records.push_back(std::move(record));
        }

        return records;
    }

private:
    std::string logFile_;
    std::ofstream logStream_;
    std::mutex mutex_;
};