#ifndef LOG_MANAGER_H
#define LOG_MANAGER_H

#include "common/config.h"
#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
#include <vector>
#include <iostream>

namespace minidb {

enum class LogRecordType { BEGIN = 0, COMMIT = 1, ABORT = 2, UPDATE = 3, CLR = 4 };

struct LogRecord {
    lsn_t lsn{0};
    lsn_t prev_lsn{0};
    txn_id_t txn_id{0};
    LogRecordType type;
    
    // For UPDATE / CLR
    page_id_t page_id{INVALID_PAGE_ID};
    uint32_t offset{0};
    std::string before_image;
    std::string after_image;
    
    // For CLR
    lsn_t undo_next_lsn{0};

    // Constructors
    LogRecord() = default;

    // Helper to construct BEGIN/COMMIT/ABORT
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type)
        : prev_lsn(prev_lsn), txn_id(txn_id), type(type) {}

    // Helper to construct UPDATE
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type, page_id_t page_id, uint32_t offset,
              const std::string& before_image, const std::string& after_image)
        : prev_lsn(prev_lsn), txn_id(txn_id), type(type), page_id(page_id), offset(offset),
          before_image(before_image), after_image(after_image) {}

    // Helper to construct CLR
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type, page_id_t page_id, uint32_t offset,
              const std::string& before_image, const std::string& after_image, lsn_t undo_next_lsn)
        : prev_lsn(prev_lsn), txn_id(txn_id), type(type), page_id(page_id), offset(offset),
          before_image(before_image), after_image(after_image), undo_next_lsn(undo_next_lsn) {}

    void Serialize(std::ostream& os) const {
        os.write(reinterpret_cast<const char*>(&lsn), sizeof(lsn));
        os.write(reinterpret_cast<const char*>(&prev_lsn), sizeof(prev_lsn));
        os.write(reinterpret_cast<const char*>(&txn_id), sizeof(txn_id));
        int32_t type_val = static_cast<int32_t>(type);
        os.write(reinterpret_cast<const char*>(&type_val), sizeof(type_val));

        if (type == LogRecordType::UPDATE || type == LogRecordType::CLR) {
            os.write(reinterpret_cast<const char*>(&page_id), sizeof(page_id));
            os.write(reinterpret_cast<const char*>(&offset), sizeof(offset));
            uint32_t before_len = before_image.length();
            os.write(reinterpret_cast<const char*>(&before_len), sizeof(before_len));
            if (before_len > 0) {
                os.write(before_image.data(), before_len);
            }
            uint32_t after_len = after_image.length();
            os.write(reinterpret_cast<const char*>(&after_len), sizeof(after_len));
            if (after_len > 0) {
                os.write(after_image.data(), after_len);
            }
        }
        if (type == LogRecordType::CLR) {
            os.write(reinterpret_cast<const char*>(&undo_next_lsn), sizeof(undo_next_lsn));
        }
    }

    bool Deserialize(std::istream& is) {
        if (!is.read(reinterpret_cast<char*>(&lsn), sizeof(lsn))) return false;
        is.read(reinterpret_cast<char*>(&prev_lsn), sizeof(prev_lsn));
        is.read(reinterpret_cast<char*>(&txn_id), sizeof(txn_id));
        int32_t type_val;
        is.read(reinterpret_cast<char*>(&type_val), sizeof(type_val));
        type = static_cast<LogRecordType>(type_val);

        if (type == LogRecordType::UPDATE || type == LogRecordType::CLR) {
            is.read(reinterpret_cast<char*>(&page_id), sizeof(page_id));
            is.read(reinterpret_cast<char*>(&offset), sizeof(offset));
            uint32_t before_len = 0;
            is.read(reinterpret_cast<char*>(&before_len), sizeof(before_len));
            before_image.resize(before_len);
            if (before_len > 0) {
                is.read(&before_image[0], before_len);
            }
            uint32_t after_len = 0;
            is.read(reinterpret_cast<char*>(&after_len), sizeof(after_len));
            after_image.resize(after_len);
            if (after_len > 0) {
                is.read(&after_image[0], after_len);
            }
        }
        if (type == LogRecordType::CLR) {
            is.read(reinterpret_cast<char*>(&undo_next_lsn), sizeof(undo_next_lsn));
        }
        return true;
    }
};

class LogManager {
public:
    explicit LogManager(const std::string& log_file) : log_file_name_(log_file), next_lsn_(1) {
        // Open/create log file in read/write/append mode
        log_io_.open(log_file_name_, std::ios::binary | std::ios::in | std::ios::out);
        if (!log_io_.is_open()) {
            // Try trunc/out if it doesn't exist
            log_io_.open(log_file_name_, std::ios::binary | std::ios::trunc | std::ios::out | std::ios::in);
        }
        
        // Scan to find the last LSN
        std::ifstream is(log_file_name_, std::ios::binary);
        if (is.is_open()) {
            LogRecord rec;
            lsn_t last_lsn = 0;
            while (rec.Deserialize(is)) {
                last_lsn = rec.lsn;
            }
            if (last_lsn > 0) {
                next_lsn_ = last_lsn + 1;
            }
        }
    }
    
    ~LogManager() {
        Flush();
        if (log_io_.is_open()) {
            log_io_.close();
        }
    }
    
    lsn_t AppendRecord(LogRecord& record) {
        std::lock_guard<std::mutex> guard(latch_);
        record.lsn = next_lsn_++;
        
        // Seek to the end before writing
        log_io_.clear();
        log_io_.seekp(0, std::ios::end);
        record.Serialize(log_io_);
        log_io_.flush();
        return record.lsn;
    }
    
    void Flush() {
        std::lock_guard<std::mutex> guard(latch_);
        if (log_io_.is_open()) {
            log_io_.flush();
        }
    }
    
    lsn_t GetNextLSN() const { return next_lsn_; }
    std::string GetLogFileName() const { return log_file_name_; }
    
private:
    std::string log_file_name_;
    std::fstream log_io_;
    std::atomic<lsn_t> next_lsn_;
    std::mutex latch_;
};

} // namespace minidb

#endif // LOG_MANAGER_H
