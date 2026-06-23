#pragma once
#include <string>
#include <fstream>
#include <mutex>

class LogManager {
private:
    std::ofstream log_file;
    std::mutex log_lock;

public:
    LogManager(const std::string& filename) {
        log_file.open(filename, std::ios::app);
    }

    ~LogManager() {
        if (log_file.is_open()) {
            log_file.close();
        }
    }

    // Appends the SQL statement to the log and forces a disk flush
    void writeLog(const std::string& sql_statement) {
        std::lock_guard<std::mutex> lock(log_lock);
        log_file << sql_statement << "\n";
        log_file.flush(); // Crucial: Ensures WAL is on disk before network send
    }
};
