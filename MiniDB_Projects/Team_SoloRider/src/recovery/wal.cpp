#include "recovery/wal.h"

namespace minidb {

WriteAheadLog::WriteAheadLog(const std::string& log_path) : log_path_(log_path), next_lsn_(1) {
    file_.open(log_path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!file_.is_open()) {
        file_.open(log_path_, std::ios::out | std::ios::binary);
        file_.close();
        file_.open(log_path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
}

WriteAheadLog::~WriteAheadLog() {
    flush();
    if (file_.is_open()) file_.close();
}

lsn_t WriteAheadLog::append(const LogRecord& record) {
    std::unique_lock<std::mutex> lock(latch_);
    lsn_t lsn = next_lsn_++;
    
    file_.write(reinterpret_cast<const char*>(&lsn), sizeof(lsn));
    file_.write(reinterpret_cast<const char*>(&record.txn_id), sizeof(record.txn_id));
    file_.write(reinterpret_cast<const char*>(&record.type), sizeof(record.type));
    file_.write(reinterpret_cast<const char*>(&record.rid), sizeof(record.rid));
    
    size_t size = record.tuple_data.size();
    file_.write(reinterpret_cast<const char*>(&size), sizeof(size));
    if (size > 0) {
        file_.write(record.tuple_data.data(), size);
    }
    return lsn;
}

void WriteAheadLog::flush() {
    std::unique_lock<std::mutex> lock(latch_);
    file_.flush();
}

std::vector<LogRecord> WriteAheadLog::read_all() {
    std::unique_lock<std::mutex> lock(latch_);
    std::vector<LogRecord> records;
    file_.seekg(0, std::ios::beg);
    
    while (file_.peek() != EOF) {
        LogRecord rec;
        file_.read(reinterpret_cast<char*>(&rec.lsn), sizeof(rec.lsn));
        if (file_.eof()) break;
        file_.read(reinterpret_cast<char*>(&rec.txn_id), sizeof(rec.txn_id));
        file_.read(reinterpret_cast<char*>(&rec.type), sizeof(rec.type));
        file_.read(reinterpret_cast<char*>(&rec.rid), sizeof(rec.rid));
        
        size_t size;
        file_.read(reinterpret_cast<char*>(&size), sizeof(size));
        if (size > 0) {
            rec.tuple_data.resize(size);
            file_.read(rec.tuple_data.data(), size);
        }
        records.push_back(rec);
    }
    file_.clear(); // clear EOF flag
    file_.seekp(0, std::ios::end); // return to append mode
    return records;
}

} // namespace minidb
