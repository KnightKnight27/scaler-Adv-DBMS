#pragma once

#include "storage/page.h"
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <atomic>

enum class LogRecordType : uint8_t {
    BEGIN = 0,
    COMMIT = 1,
    ABORT = 2,
    INSERT = 3,
    DELETE = 4,
    UPDATE = 5,
    CLR = 6
};

struct LogRecord {
    Lsn_t lsn = 0;
    Lsn_t prev_lsn = 0;
    TxId_t txn_id = 0;
    LogRecordType type = LogRecordType::BEGIN;

    // Data operations
    PageId_t page_id = INVALID_PAGE_ID;
    uint16_t slot_id = 0;
    std::string before_image; // Before-image
    std::string after_image;  // After-image

    // For CLR
    Lsn_t undo_next_lsn = 0;

    std::vector<char> Serialize() const;
    static LogRecord Deserialize(std::istream& is);
};

class LogManager {
public:
    explicit LogManager(const std::string& log_file);
    ~LogManager();

    Lsn_t AppendRecord(TxId_t txn_id, Lsn_t prev_lsn, LogRecordType type,
                        PageId_t page_id = INVALID_PAGE_ID, uint16_t slot_id = 0,
                        const std::string& before_image = "", const std::string& after_image = "",
                        Lsn_t undo_next_lsn = 0);

    void Flush();
    void FlushUpTo(Lsn_t lsn);

    Lsn_t GetFlushedLsn() const { return flushed_lsn_; }
    void Clear(); // Wipes WAL for fresh runs/tests

private:
    std::string log_file_name_;
    std::ofstream log_file_;
    std::atomic<Lsn_t> next_lsn_{1};
    std::atomic<Lsn_t> flushed_lsn_{0};
    
    std::vector<char> log_buffer_;
    std::mutex latch_;
};
