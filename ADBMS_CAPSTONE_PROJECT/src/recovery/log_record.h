#pragma once
#include "storage/tuple.h"
#include <string>

namespace minidb {

enum class LogRecordType {
    BEGIN,
    COMMIT,
    ABORT,
    INSERT,
    DELETE
};

// Logical Log Record
struct LogRecord {
    int txn_id_;
    LogRecordType type_;
    
    // Only used for INSERT / DELETE operations
    Tuple tuple_;
    
    // Size is fixed for simplicity, though real systems use variable length
    LogRecord() = default;
    
    LogRecord(int txn_id, LogRecordType type) 
        : txn_id_(txn_id), type_(type) {}
        
    LogRecord(int txn_id, LogRecordType type, const Tuple& tuple)
        : txn_id_(txn_id), type_(type), tuple_(tuple) {}
};

} // namespace minidb
