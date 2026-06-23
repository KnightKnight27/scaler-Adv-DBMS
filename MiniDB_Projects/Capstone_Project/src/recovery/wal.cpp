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
    fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::cerr << "[WAL] ERROR: Cannot open WAL file: " << path << "\n";
        return false;
    }

    const off_t file_size = ::lseek(fd_, 0, SEEK_END);
    if (file_size > 0) {
        const LSN num_records = static_cast<LSN>(file_size / WAL_RECORD_SIZE);
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
    const ssize_t written = ::write(fd_, &rec, WAL_RECORD_SIZE);
    if (written != static_cast<ssize_t>(WAL_RECORD_SIZE)) {
        std::cerr << "[WAL] ERROR: Could not write full WAL record\n";
        return INVALID_LSN;
    }
    return rec.lsn;
}

LSN WAL::logBegin(TxID txid) {
    WALRecord rec;
    rec.lsn = current_lsn_.fetch_add(1) + 1;
    rec.txid = txid;
    rec.type = WALRecordType::BEGIN;
    return appendRecord(rec);
}

LSN WAL::logInsert(TxID txid, const std::string& table, int32_t key, const std::string& value) {
    WALRecord rec;
    rec.lsn = current_lsn_.fetch_add(1) + 1;
    rec.txid = txid;
    rec.type = WALRecordType::INSERT;
    rec.key = key;
    std::strncpy(rec.table_name, table.c_str(), MAX_TABLE_NAME_LEN - 1);
    std::strncpy(rec.value, value.c_str(), WAL_MAX_VALUE_SIZE - 1);
    return appendRecord(rec);
}

LSN WAL::logDelete(TxID txid, const std::string& table, int32_t key) {
    WALRecord rec;
    rec.lsn = current_lsn_.fetch_add(1) + 1;
    rec.txid = txid;
    rec.type = WALRecordType::DELETE;
    rec.key = key;
    std::strncpy(rec.table_name, table.c_str(), MAX_TABLE_NAME_LEN - 1);
    return appendRecord(rec);
}

LSN WAL::logCommit(TxID txid) {
    WALRecord rec;
    rec.lsn = current_lsn_.fetch_add(1) + 1;
    rec.txid = txid;
    rec.type = WALRecordType::COMMIT;
    const LSN lsn = appendRecord(rec);
    flush();
    return lsn;
}

LSN WAL::logAbort(TxID txid) {
    WALRecord rec;
    rec.lsn = current_lsn_.fetch_add(1) + 1;
    rec.txid = txid;
    rec.type = WALRecordType::ABORT;
    return appendRecord(rec);
}
