#include "LogManager.h"

#include <stdexcept>

// ── Constructor / Destructor ────────────────────────────────────────────

LogManager::LogManager(const std::string& walFilePath)
    : walFilePath_(walFilePath), nextLSN_(0)
{
    // Create the WAL file if it doesn't exist
    {
        std::ofstream creator(walFilePath, std::ios::app | std::ios::binary);
        if (!creator.is_open()) {
            throw std::runtime_error("Cannot create WAL file: " + walFilePath);
        }
    }

    // Open for both reading and writing (append mode for writes)
    walFile_.open(walFilePath,
                  std::ios::in | std::ios::out | std::ios::binary);
    if (!walFile_.is_open()) {
        throw std::runtime_error("Cannot open WAL file: " + walFilePath);
    }

    // Determine the next LSN from the existing file size
    // Each log record is exactly SERIALIZED_SIZE bytes
    walFile_.seekg(0, std::ios::end);
    long fileSize = walFile_.tellg();
    if (fileSize > 0) {
        nextLSN_ = static_cast<int>(fileSize / LogRecord::SERIALIZED_SIZE);
    }
}

LogManager::~LogManager() {
    if (walFile_.is_open()) {
        walFile_.flush();
        walFile_.close();
    }
}

// ── Append ──────────────────────────────────────────────────────────────

int LogManager::appendLog(LogRecord& record) {
    std::lock_guard<std::mutex> guard(mutex_);

    record.lsn = nextLSN_++;

    // Seek to end and write
    walFile_.seekp(0, std::ios::end);
    record.serialize(walFile_);

    if (!walFile_.good()) {
        throw std::runtime_error("Failed to write log record LSN=" +
                                 std::to_string(record.lsn));
    }

    return record.lsn;
}

// ── Flush ───────────────────────────────────────────────────────────────

void LogManager::flush() {
    std::lock_guard<std::mutex> guard(mutex_);
    walFile_.flush();
}

// ── Read all logs ───────────────────────────────────────────────────────

std::vector<LogRecord> LogManager::readAllLogs() {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<LogRecord> logs;

    // Open a separate read stream to avoid disturbing the write position
    std::ifstream reader(walFilePath_, std::ios::binary);
    if (!reader.is_open()) {
        return logs;
    }

    LogRecord rec;
    while (reader.peek() != EOF && LogRecord::deserialize(reader, rec)) {
        logs.push_back(rec);
    }

    return logs;
}
