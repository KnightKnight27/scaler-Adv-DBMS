#include "recovery/log_manager.h"
#include <iostream>
#include <cstring>

std::vector<char> LogRecord::Serialize() const {
    std::vector<char> bytes;
    
    // Resize vector to accommodate fixed header size
    // lsn(8) + prev_lsn(8) + txn_id(4) + type(1) + page_id(4) + slot_id(2)
    size_t header_size = 8 + 8 + 4 + 1 + 4 + 2;
    bytes.resize(header_size);

    char* ptr = bytes.data();
    std::memcpy(ptr, &lsn, 8); ptr += 8;
    std::memcpy(ptr, &prev_lsn, 8); ptr += 8;
    std::memcpy(ptr, &txn_id, 4); ptr += 4;
    uint8_t t = static_cast<uint8_t>(type);
    std::memcpy(ptr, &t, 1); ptr += 1;
    std::memcpy(ptr, &page_id, 4); ptr += 4;
    std::memcpy(ptr, &slot_id, 2); ptr += 2;

    // Append variable fields
    if (type == LogRecordType::INSERT || type == LogRecordType::DELETE || 
        type == LogRecordType::UPDATE || type == LogRecordType::CLR) {
        
        uint16_t before_len = static_cast<uint16_t>(before_image.length());
        char temp[2];
        std::memcpy(temp, &before_len, 2);
        bytes.insert(bytes.end(), temp, temp + 2);
        bytes.insert(bytes.end(), before_image.begin(), before_image.end());

        uint16_t after_len = static_cast<uint16_t>(after_image.length());
        std::memcpy(temp, &after_len, 2);
        bytes.insert(bytes.end(), temp, temp + 2);
        bytes.insert(bytes.end(), after_image.begin(), after_image.end());
    }

    if (type == LogRecordType::CLR) {
        char temp[8];
        std::memcpy(temp, &undo_next_lsn, 8);
        bytes.insert(bytes.end(), temp, temp + 8);
    }

    return bytes;
}

LogRecord LogRecord::Deserialize(std::istream& is) {
    LogRecord record;
    
    // Read fixed header
    char header[27];
    is.read(header, 27);
    if (!is) {
        record.lsn = 0; // Means EOF
        return record;
    }

    const char* ptr = header;
    std::memcpy(&record.lsn, ptr, 8); ptr += 8;
    std::memcpy(&record.prev_lsn, ptr, 8); ptr += 8;
    std::memcpy(&record.txn_id, ptr, 4); ptr += 4;
    uint8_t t;
    std::memcpy(&t, ptr, 1); ptr += 1;
    record.type = static_cast<LogRecordType>(t);
    std::memcpy(&record.page_id, ptr, 4); ptr += 4;
    std::memcpy(&record.slot_id, ptr, 2); ptr += 2;

    // Read variable fields
    if (record.type == LogRecordType::INSERT || record.type == LogRecordType::DELETE || 
        record.type == LogRecordType::UPDATE || record.type == LogRecordType::CLR) {
        
        uint16_t before_len = 0;
        is.read(reinterpret_cast<char*>(&before_len), 2);
        if (before_len > 0) {
            record.before_image.resize(before_len);
            is.read(&record.before_image[0], before_len);
        }

        uint16_t after_len = 0;
        is.read(reinterpret_cast<char*>(&after_len), 2);
        if (after_len > 0) {
            record.after_image.resize(after_len);
            is.read(&record.after_image[0], after_len);
        }
    }

    if (record.type == LogRecordType::CLR) {
        is.read(reinterpret_cast<char*>(&record.undo_next_lsn), 8);
    }

    return record;
}

LogManager::LogManager(const std::string& log_file) : log_file_name_(log_file) {
    // Open log file in append mode to verify access, then close immediately
    log_file_.open(log_file_name_, std::ios::binary | std::ios::out | std::ios::app);
    if (!log_file_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + log_file_name_);
    }
    log_file_.close(); // Keep closed by default to prevent locking on Windows

    // Determine the next LSN by reading existing records
    std::ifstream is(log_file_name_, std::ios::binary);
    if (is.is_open()) {
        Lsn_t last_lsn = 0;
        while (is) {
            LogRecord rec = LogRecord::Deserialize(is);
            if (rec.lsn > 0) {
                last_lsn = rec.lsn;
            }
        }
        next_lsn_ = last_lsn + 1;
        flushed_lsn_ = last_lsn;
    }
}

LogManager::~LogManager() {
    Flush();
}

Lsn_t LogManager::AppendRecord(TxId_t txn_id, Lsn_t prev_lsn, LogRecordType type,
                               PageId_t page_id, uint16_t slot_id,
                               const std::string& before_image, const std::string& after_image,
                               Lsn_t undo_next_lsn) {
    std::lock_guard<std::mutex> lock(latch_);

    LogRecord rec;
    rec.lsn = next_lsn_++;
    rec.prev_lsn = prev_lsn;
    rec.txn_id = txn_id;
    rec.type = type;
    rec.page_id = page_id;
    rec.slot_id = slot_id;
    rec.before_image = before_image;
    rec.after_image = after_image;
    rec.undo_next_lsn = undo_next_lsn;

    std::vector<char> bytes = rec.Serialize();
    log_buffer_.insert(log_buffer_.end(), bytes.begin(), bytes.end());

    // Flush immediately on commit/abort to ensure durability
    if (type == LogRecordType::COMMIT || type == LogRecordType::ABORT) {
        log_file_.open(log_file_name_, std::ios::binary | std::ios::out | std::ios::app);
        if (log_file_.is_open()) {
            log_file_.write(log_buffer_.data(), log_buffer_.size());
            log_file_.close();
        }
        log_buffer_.clear();
        flushed_lsn_ = rec.lsn;
    }

    return rec.lsn;
}

void LogManager::Flush() {
    std::lock_guard<std::mutex> lock(latch_);
    if (log_buffer_.empty()) return;

    log_file_.open(log_file_name_, std::ios::binary | std::ios::out | std::ios::app);
    if (log_file_.is_open()) {
        log_file_.write(log_buffer_.data(), log_buffer_.size());
        log_file_.close();
    }
    log_buffer_.clear();
    flushed_lsn_ = next_lsn_ - 1;
}

void LogManager::FlushUpTo(Lsn_t lsn) {
    if (lsn <= flushed_lsn_) return;
    Flush();
}

void LogManager::Clear() {
    std::lock_guard<std::mutex> lock(latch_);
    // Truncate the file and close it immediately
    log_file_.open(log_file_name_, std::ios::binary | std::ios::out | std::ios::trunc);
    log_file_.close();

    next_lsn_ = 1;
    flushed_lsn_ = 0;
    log_buffer_.clear();
}
