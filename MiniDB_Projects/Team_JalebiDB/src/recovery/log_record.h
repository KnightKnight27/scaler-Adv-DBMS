#pragma once

#include "common/config.h"
#include "common/types.h"
#include <cstring>
#include <string>

namespace minidb {

enum class LogRecordType {
    INVALID = 0,
    BEGIN,
    COMMIT,
    ABORT,
    INSERT,
    DELETE
};

class LogRecord {
public:
    LogRecord() = default;
    
    // Constructor for BEGIN/COMMIT/ABORT
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type)
        : size_(HEADER_SIZE), lsn_(INVALID_LSN), prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type) {}

    // Constructor for INSERT/DELETE
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type, RID rid, const std::string &data)
        : lsn_(INVALID_LSN), prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type), rid_(rid), data_(data) {
        size_ = HEADER_SIZE + sizeof(page_id_t) + sizeof(slot_id_t) + sizeof(uint16_t) + data.size();
    }

    // Getters
    uint32_t GetSize() const { return size_; }
    lsn_t GetLSN() const { return lsn_; }
    void SetLSN(lsn_t lsn) { lsn_ = lsn; }
    lsn_t GetPrevLSN() const { return prev_lsn_; }
    txn_id_t GetTxnId() const { return txn_id_; }
    LogRecordType GetType() const { return type_; }
    RID GetRID() const { return rid_; }
    const std::string &GetData() const { return data_; }

    // Serialization
    void Serialize(char *dest) const {
        std::memcpy(dest, &size_, sizeof(uint32_t));
        std::memcpy(dest + 4, &lsn_, sizeof(lsn_t));
        std::memcpy(dest + 12, &prev_lsn_, sizeof(lsn_t));
        std::memcpy(dest + 20, &txn_id_, sizeof(txn_id_t));
        int type_val = static_cast<int>(type_);
        std::memcpy(dest + 24, &type_val, sizeof(int));

        if (type_ == LogRecordType::INSERT || type_ == LogRecordType::DELETE) {
            std::memcpy(dest + HEADER_SIZE, &rid_.page_id, sizeof(page_id_t));
            std::memcpy(dest + HEADER_SIZE + 4, &rid_.slot_id, sizeof(slot_id_t));
            uint16_t len = static_cast<uint16_t>(data_.size());
            std::memcpy(dest + HEADER_SIZE + 8, &len, sizeof(uint16_t));
            std::memcpy(dest + HEADER_SIZE + 10, data_.data(), len);
        }
    }

    // Deserialization
    static LogRecord Deserialize(const char *src) {
        LogRecord rec;
        std::memcpy(&rec.size_, src, sizeof(uint32_t));
        std::memcpy(&rec.lsn_, src + 4, sizeof(lsn_t));
        std::memcpy(&rec.prev_lsn_, src + 12, sizeof(lsn_t));
        std::memcpy(&rec.txn_id_, src + 20, sizeof(txn_id_t));
        int type_val;
        std::memcpy(&type_val, src + 24, sizeof(int));
        rec.type_ = static_cast<LogRecordType>(type_val);

        if (rec.type_ == LogRecordType::INSERT || rec.type_ == LogRecordType::DELETE) {
            std::memcpy(&rec.rid_.page_id, src + HEADER_SIZE, sizeof(page_id_t));
            std::memcpy(&rec.rid_.slot_id, src + HEADER_SIZE + 4, sizeof(slot_id_t));
            uint16_t len;
            std::memcpy(&len, src + HEADER_SIZE + 8, sizeof(uint16_t));
            rec.data_ = std::string(src + HEADER_SIZE + 10, len);
        }
        return rec;
    }

    std::string ToString() const {
        std::string s = "LSN: " + std::to_string(lsn_) + ", TxnID: " + std::to_string(txn_id_) + ", Type: ";
        switch (type_) {
            case LogRecordType::BEGIN: s += "BEGIN"; break;
            case LogRecordType::COMMIT: s += "COMMIT"; break;
            case LogRecordType::ABORT: s += "ABORT"; break;
            case LogRecordType::INSERT: s += "INSERT (RID=" + rid_.ToString() + ")"; break;
            case LogRecordType::DELETE: s += "DELETE (RID=" + rid_.ToString() + ")"; break;
            default: s += "UNKNOWN"; break;
        }
        return s;
    }

    static constexpr uint32_t HEADER_SIZE = 28; // size + lsn + prev_lsn + txn_id + type

private:
    uint32_t size_{0};
    lsn_t lsn_{INVALID_LSN};
    lsn_t prev_lsn_{INVALID_LSN};
    txn_id_t txn_id_{INVALID_TXN_ID};
    LogRecordType type_{LogRecordType::INVALID};

    // Body fields (only for INSERT/DELETE)
    RID rid_;
    std::string data_;
};

} // namespace minidb
