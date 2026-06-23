#pragma once

#include "concurrency/lock_manager.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace minidb {

enum class LogRecordType : uint8_t {
    BEGIN = 1,
    COMMIT = 2,
    ABORT = 3,
    INSERT_ROW = 4,
    UPDATE_XMAX = 5,
};

struct LogRecord {
    uint64_t lsn = 0;
    LogRecordType type = LogRecordType::BEGIN;
    std::vector<char> payload;
};

class LogManager {
public:
    explicit LogManager(const std::string& log_path);
    ~LogManager();

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    uint64_t Append(LogRecordType type, const std::vector<char>& payload);
    void Flush();
    std::vector<LogRecord> LoadAll() const;
    uint64_t GetNextLSN() const;

    static std::vector<char> EncodeBegin(TxID tx_id);
    static std::vector<char> EncodeCommit(TxID tx_id);
    static std::vector<char> EncodeAbort(TxID tx_id);
    static std::vector<char> EncodeInsertRow(TxID tx_id, int32_t page_id, uint16_t slot_index,
                                             uint16_t row_offset, uint16_t row_length,
                                             const std::string& key, uint64_t xmin, uint64_t xmax,
                                             uint64_t prev_version_tid, const std::string& value);
    static std::vector<char> EncodeUpdateXmax(TxID tx_id, int32_t page_id, uint16_t slot_index,
                                              uint64_t old_xmax, uint64_t new_xmax);

    static TxID DecodeTxId(const std::vector<char>& payload);
    static void DecodeInsertRow(const std::vector<char>& payload, TxID& tx_id, int32_t& page_id,
                                uint16_t& slot_index, uint16_t& row_offset, uint16_t& row_length,
                                std::string& key, uint64_t& xmin, uint64_t& xmax,
                                uint64_t& prev_version_tid, std::string& value);
    static void DecodeUpdateXmax(const std::vector<char>& payload, TxID& tx_id, int32_t& page_id,
                                 uint16_t& slot_index, uint64_t& old_xmax, uint64_t& new_xmax);

private:
    void AppendBytes(const void* data, std::size_t size);

    int fd_;
    std::string log_path_;
    uint64_t next_lsn_;
    mutable std::mutex mu_;
};

}  // namespace minidb
