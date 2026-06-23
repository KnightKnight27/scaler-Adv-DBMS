#include "recovery/wal.h"
#include "common/logger.h"
#include <filesystem>

namespace minidb {

WAL::WAL(const std::string& file_path) : log_file_path_(file_path) {
    log_stream_.open(log_file_path_, std::ios::app | std::ios::binary);
}

WAL::~WAL() {
    if (log_stream_.is_open()) {
        Flush(); // Ensure we don't lose data on shutdown
        log_stream_.close();
    }
}

lsn_t WAL::Append(LogRecordType type, const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    current_lsn_++;
    uint32_t key_size = key.size();
    uint32_t val_size = value.size();

    log_stream_.write(reinterpret_cast<const char*>(&current_lsn_), sizeof(current_lsn_));
    log_stream_.write(reinterpret_cast<const char*>(&type), sizeof(type));
    log_stream_.write(reinterpret_cast<const char*>(&key_size), sizeof(key_size));
    log_stream_.write(key.data(), key_size);
    log_stream_.write(reinterpret_cast<const char*>(&val_size), sizeof(val_size));
    
    if (val_size > 0) {
        log_stream_.write(value.data(), val_size);
    }

    // Notice: We REMOVED log_stream_.flush() from here!
    return current_lsn_;
}

void WAL::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    log_stream_.flush();
}

void WAL::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    log_stream_.close();
    
    // FIX: Catch OS-level filesystem exceptions safely
    std::error_code ec;
    std::filesystem::remove(log_file_path_, ec);
    if (ec) {
        // Log the error but prevent the engine from fatally crashing
        LOG_ERROR("Failed to remove WAL file during Clear(): " + ec.message());
    }

    log_stream_.open(log_file_path_, std::ios::app | std::ios::binary);
    current_lsn_ = 0;
}

} // namespace minidb