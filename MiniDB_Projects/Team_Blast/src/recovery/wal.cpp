#include "recovery/wal.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <iostream>

WAL::~WAL() {
    close();
}

bool WAL::open(const std::string& path) {
    path_ = path;
    // Open for append + read; create if not exists
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[WAL] ERROR: cannot open WAL file: " << path << "\n";
        return false;
    }

    // Find the highest LSN already in the file (for recovery restarts)
    off_t file_size = ::lseek(fd_, 0, SEEK_END);
    if (file_size > 0) {
        LSN num_records = static_cast<LSN>(file_size / WAL_RECORD_SIZE);
        current_lsn_.store(num_records);
    }
    return true;
}

void WAL::close() {
    if (fd_ >= 0) {
        flush();
        ::close(fd_);
        fd_ = -1;
    }
}

void WAL::flush() {
    if (fd_ >= 0) {
        ::fsync(fd_);
    }
}

LSN WAL::appendRecord(const WALRecord& rec) {
    std::lock_guard<std::mutex> lk(write_mutex_);
    ssize_t written = ::write(fd_, &rec, WAL_RECORD_SIZE);
    if (written != static_cast<ssize_t>(WAL_RECORD_SIZE)) {
        std::cerr << "[WAL] ERROR: could not write full record\n";
        return INVALID_LSN;
    }
    return rec.lsn;
}

LSN WAL::logBegin(TxID txid) {
    WALRecord r;
    r.lsn  = current_lsn_.fetch_add(1) + 1;
    r.txid = txid;
    r.type = WALRecordType::BEGIN;
    return appendRecord(r);
}

LSN WAL::logInsert(TxID txid, const std::string& table, int32_t key, const std::string& value) {
    WALRecord r;
    r.lsn  = current_lsn_.fetch_add(1) + 1;
    r.txid = txid;
    r.type = WALRecordType::INSERT;
    r.key  = key;
    std::strncpy(r.table_name, table.c_str(), MAX_TABLE_NAME_LEN - 1);
    std::strncpy(r.value, value.c_str(), WAL_MAX_VALUE_SIZE - 1);
    return appendRecord(r);
}

LSN WAL::logDelete(TxID txid, const std::string& table, int32_t key) {
    WALRecord r;
    r.lsn  = current_lsn_.fetch_add(1) + 1;
    r.txid = txid;
    r.type = WALRecordType::DELETE;
    r.key  = key;
    std::strncpy(r.table_name, table.c_str(), MAX_TABLE_NAME_LEN - 1);
    return appendRecord(r);
}

LSN WAL::logCommit(TxID txid) {
    WALRecord r;
    r.lsn  = current_lsn_.fetch_add(1) + 1;
    r.txid = txid;
    r.type = WALRecordType::COMMIT;
    LSN lsn = appendRecord(r);
    flush();  // fsync on commit — durability guarantee
    return lsn;
}

LSN WAL::logAbort(TxID txid) {
    WALRecord r;
    r.lsn  = current_lsn_.fetch_add(1) + 1;
    r.txid = txid;
    r.type = WALRecordType::ABORT;
    return appendRecord(r);
}
