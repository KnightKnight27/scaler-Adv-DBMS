#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include "common/config.h"

namespace minidb {

enum class LogRecordType : uint8_t {
    PUT = 0,
    DELETE_TOMBSTONE = 1
};

class WAL {
private:
    std::string log_file_path_;
    std::ofstream log_stream_;
    std::mutex mutex_;
    lsn_t current_lsn_{0};

public:
    explicit WAL(const std::string& file_path);
    ~WAL();

    // Appends to the log buffer (DOES NOT FLUSH)
    lsn_t Append(LogRecordType type, const std::string& key, const std::string& value);
    
    // Forces the OS to flush the buffer to physical disk
    void Flush();

    void Clear();
};

} // namespace minidb