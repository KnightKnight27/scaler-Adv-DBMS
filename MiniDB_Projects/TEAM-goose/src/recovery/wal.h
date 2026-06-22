#pragma once

#include "common/types.h"
#include <fstream>
#include <vector>
#include <string>
#include <filesystem>
#include <functional>

namespace minidb {

// wal — write ahead logging
// a simple sequential log that records all modifications before they are
// applied to the storage engine.  used for crash recovery.
//
// log record format (binary):
//   [lsn (u64)] [txn_id (u32)] [type (u8)] [table_id (u32)]
//   [key_value] [before_record] [after_record] [checksum (u32)]
//
// types: 1=begin, 2=update, 3=commit, 4=abort

enum class LogRecordType : uint8_t {
    BEGIN  = 1,
    UPDATE = 2,
    COMMIT = 3,
    ABORT  = 4
};

struct LogRecord {
    LSN             lsn = 0;
    TxnID           txn_id = INVALID_TXN;
    LogRecordType   type = LogRecordType::UPDATE;
    TableID         table_id = 0;
    Key             key;
    Record          before_image;
    Record          after_image;
};

class WriteAheadLog {
public:
    WriteAheadLog(const std::string& log_file);

    // append a log record.  returns the assigned lsn.
    LSN append(LogRecordType type, TxnID txn_id, TableID table_id,
               const Key& key, const Record& before, const Record& after);

    // flush pending writes to disk (fsync).
    void flush();

    // read all log records from the file (used during recovery).
    std::vector<LogRecord> read_all() const;

    // current lsn counter.
    LSN current_lsn() const { return _current_lsn; }

    // close the log file.
    void close();

private:
    std::string   _filepath;
    std::ofstream _file;
    LSN           _current_lsn = 0;
    mutable std::mutex _mutex;
};

// recoverymanager — crash recovery using wal

class LSMEngine;  // forward decl
class TransactionManager;

class RecoveryManager {
public:
    RecoveryManager(WriteAheadLog& wal, LSMEngine& storage,
                    TransactionManager& txn_mgr);

    // perform crash recovery:
    //   1. analysis: scan wal, identify active transactions at crash time
    //   2. redo: replay committed transaction operations
    //   3. undo: revert uncommitted transaction operations
    // returns true on success.
    bool recover();

private:
    WriteAheadLog&      _wal;
    LSMEngine&          _storage;
    TransactionManager& _txn_mgr;
};

// wal implementation

inline WriteAheadLog::WriteAheadLog(const std::string& log_file)
    : _filepath(log_file) {
    std::filesystem::create_directories(
        std::filesystem::path(log_file).parent_path());
    _file.open(log_file, std::ios::binary | std::ios::app);
    if (!_file.good()) {
        throw std::runtime_error("Cannot open WAL file: " + log_file);
    }
    // calculate current lsn from existing file size
    _file.seekp(0, std::ios::end);
    // each log record is variable size, but we'll approximate.
    // for simplicity, start lsn at 1 and increment.
    _current_lsn = 1;
}

inline LSN WriteAheadLog::append(LogRecordType type, TxnID txn_id,
                                  TableID table_id, const Key& key,
                                  const Record& before, const Record& after) {
    std::lock_guard<std::mutex> lock(_mutex);

    LSN lsn = _current_lsn++;

    // serialise log record to a buffer
    std::ostringstream oss(std::ios::binary);
    write_u64(oss, lsn);
    write_u32(oss, txn_id);
    oss.put(static_cast<char>(type));
    write_u32(oss, table_id);
    write_value(oss, key);
    write_record(oss, before);
    write_record(oss, after);

    // simple checksum: xor of all bytes
    std::string bytes = oss.str();
    uint32_t checksum = 0;
    for (char c : bytes) checksum ^= static_cast<uint8_t>(c);
    write_u32(oss, checksum);

    _file.write(oss.str().data(), static_cast<std::streamsize>(oss.str().size()));
    _file.flush(); // ensure wal is durable before returning

    return lsn;
}

inline void WriteAheadLog::flush() {
    std::lock_guard<std::mutex> lock(_mutex);
    _file.flush();
}

inline std::vector<LogRecord> WriteAheadLog::read_all() const {
    std::vector<LogRecord> records;

    std::ifstream ifs(_filepath, std::ios::binary);
    if (!ifs.good()) return records;

    while (ifs.peek() != EOF) {
        LogRecord rec;
        try {
            rec.lsn      = read_u64(ifs);
            rec.txn_id   = read_u32(ifs);
            rec.type     = static_cast<LogRecordType>(ifs.get());
            rec.table_id = read_u32(ifs);
            rec.key      = read_value(ifs);
            rec.before_image = read_record(ifs);
            rec.after_image  = read_record(ifs);
            // skip checksum for simplicity
            ifs.ignore(4);
            records.push_back(rec);
        } catch (...) {
            break; // truncated log
        }
    }
    return records;
}

inline void WriteAheadLog::close() {
    std::lock_guard<std::mutex> lock(_mutex);
    _file.close();
}

// recovery implementation

inline RecoveryManager::RecoveryManager(WriteAheadLog& wal, LSMEngine& storage,
                                         TransactionManager& txn_mgr)
    : _wal(wal), _storage(storage), _txn_mgr(txn_mgr) {}

inline bool RecoveryManager::recover() {
    auto records = _wal.read_all();
    if (records.empty()) return true; // nothing to recover

    // --- analysis phase ---
    // find:
    //   - committed transactions (have commit record)
    //   - active transactions (no commit/abort record)
    std::unordered_set<TxnID> committed_txns;
    std::unordered_set<TxnID> aborted_txns;

    for (const auto& rec : records) {
        if (rec.type == LogRecordType::COMMIT) {
            committed_txns.insert(rec.txn_id);
        } else if (rec.type == LogRecordType::ABORT) {
            aborted_txns.insert(rec.txn_id);
        }
    }

    // --- redo phase ---
    // replay all update operations from committed transactions.
    for (const auto& rec : records) {
        if (rec.type == LogRecordType::UPDATE &&
            committed_txns.count(rec.txn_id)) {
            // re-apply the after_image
            bool found = false;
            _storage.get(rec.table_id, rec.key, found);
            // always put (recover even if the record was lost)
            _storage.put(rec.table_id, rec.key, rec.after_image);
        }
    }

    // --- undo phase ---
    // roll back all operations from uncommitted transactions.
    // process in reverse order.
    for (auto rit = records.rbegin(); rit != records.rend(); ++rit) {
        if (rit->type == LogRecordType::UPDATE &&
            !committed_txns.count(rit->txn_id) &&
            !aborted_txns.count(rit->txn_id)) {
            // restore the before_image
            _storage.put(rit->table_id, rit->key, rit->before_image);
        }
    }

    return true;
}

} // namespace minidb
