#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "../types.h"

namespace minidb {

enum class LogType : int32_t {
    Begin = 1,
    Insert = 2,
    Delete = 3,
    Commit = 4,
    Abort = 5,
    Checkpoint = 6,
};

struct LogRecord {
    int64_t lsn = 0;
    int32_t txn_id = 0;
    LogType type = LogType::Begin;
    std::string table;
    RID rid;
    std::vector<uint8_t> image;  // after-image for Insert, before-image for Delete
};

// Append-only write-ahead log. Records are buffered in memory and forced to
// disk on demand; the buffer pool forces the log before writing any page,
// which is the write-ahead rule.
class LogManager {
public:
    explicit LogManager(const std::string& path) : path_(path) {
        std::ifstream in(path_, std::ios::binary);
        if (in) {
            LogRecord r;
            while (read_record(in, r)) records_.push_back(r);
        }
        next_lsn_ = static_cast<int64_t>(records_.size());
        out_.open(path_, std::ios::binary | std::ios::app);
    }

    const std::vector<LogRecord>& records() const { return records_; }

    int64_t append(LogRecord r) {
        r.lsn = next_lsn_++;
        records_.push_back(r);
        pending_.push_back(records_.back());
        return r.lsn;
    }

    // Force all buffered records to disk. The argument is the LSN that must be
    // durable; we flush everything pending, which always satisfies it.
    void flush(int64_t up_to = -1) {
        (void)up_to;
        for (const auto& r : pending_) write_record(out_, r);
        out_.flush();
        pending_.clear();
    }

private:
    std::string path_;
    std::vector<LogRecord> records_;
    std::vector<LogRecord> pending_;
    std::ofstream out_;
    int64_t next_lsn_ = 0;

    static void put_i32(std::ostream& o, int32_t v) {
        o.write(reinterpret_cast<const char*>(&v), 4);
    }
    static void put_i64(std::ostream& o, int64_t v) {
        o.write(reinterpret_cast<const char*>(&v), 8);
    }
    static bool get_i32(std::istream& i, int32_t& v) {
        return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), 4));
    }
    static bool get_i64(std::istream& i, int64_t& v) {
        return static_cast<bool>(i.read(reinterpret_cast<char*>(&v), 8));
    }

    static void write_record(std::ostream& o, const LogRecord& r) {
        put_i64(o, r.lsn);
        put_i32(o, r.txn_id);
        put_i32(o, static_cast<int32_t>(r.type));
        put_i32(o, static_cast<int32_t>(r.table.size()));
        o.write(r.table.data(), r.table.size());
        put_i32(o, r.rid.page_id);
        put_i32(o, r.rid.slot_id);
        put_i32(o, static_cast<int32_t>(r.image.size()));
        if (!r.image.empty())
            o.write(reinterpret_cast<const char*>(r.image.data()), r.image.size());
    }

    static bool read_record(std::istream& in, LogRecord& r) {
        if (!get_i64(in, r.lsn)) return false;
        int32_t type, tlen, ilen;
        if (!get_i32(in, r.txn_id)) return false;
        if (!get_i32(in, type)) return false;
        r.type = static_cast<LogType>(type);
        if (!get_i32(in, tlen)) return false;
        r.table.resize(tlen);
        if (tlen && !in.read(&r.table[0], tlen)) return false;
        if (!get_i32(in, r.rid.page_id)) return false;
        if (!get_i32(in, r.rid.slot_id)) return false;
        if (!get_i32(in, ilen)) return false;
        r.image.resize(ilen);
        if (ilen && !in.read(reinterpret_cast<char*>(r.image.data()), ilen)) return false;
        return true;
    }
};

}  // namespace minidb
