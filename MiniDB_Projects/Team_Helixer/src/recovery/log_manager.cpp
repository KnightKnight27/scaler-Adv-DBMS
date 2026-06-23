#include "recovery/log_manager.h"
#include <cstring>

namespace minidb {
namespace {

// Little helpers to pack/unpack POD values and strings into a byte buffer.
template <typename T>
void put(std::vector<char> &buf, T v) {
    const char *p = reinterpret_cast<const char *>(&v);
    buf.insert(buf.end(), p, p + sizeof(T));
}
void put_str(std::vector<char> &buf, const std::string &s) {
    put<uint32_t>(buf, (uint32_t)s.size());
    buf.insert(buf.end(), s.begin(), s.end());
}
void put_bytes(std::vector<char> &buf, const std::vector<char> &b) {
    put<uint32_t>(buf, (uint32_t)b.size());
    buf.insert(buf.end(), b.begin(), b.end());
}

template <typename T>
T get(const char *&p) { T v; std::memcpy(&v, p, sizeof(T)); p += sizeof(T); return v; }
std::string get_str(const char *&p) {
    uint32_t n = get<uint32_t>(p);
    std::string s(p, p + n); p += n; return s;
}
std::vector<char> get_bytes(const char *&p) {
    uint32_t n = get<uint32_t>(p);
    std::vector<char> b(p, p + n); p += n; return b;
}

// Serialize a record's payload (everything after the length prefix).
std::vector<char> encode(const LogRecord &r) {
    std::vector<char> b;
    put<uint8_t>(b, (uint8_t)r.type);
    put<uint64_t>(b, r.lsn);
    put<uint64_t>(b, r.txn);
    if (r.type == LogType::CREATE_TABLE) {
        put_str(b, r.table);
        put<int32_t>(b, r.schema.pk_index);
        put<uint32_t>(b, (uint32_t)r.schema.columns.size());
        for (auto &c : r.schema.columns) {
            put_str(b, c.name);
            put<uint8_t>(b, (uint8_t)c.type);
        }
    } else if (r.type == LogType::INSERT || r.type == LogType::DELETE) {
        put_str(b, r.table);
        put_bytes(b, r.tuple_bytes);
    }
    return b;
}

LogRecord decode(const char *p) {
    LogRecord r;
    r.type = (LogType)get<uint8_t>(p);
    r.lsn  = get<uint64_t>(p);
    r.txn  = get<uint64_t>(p);
    if (r.type == LogType::CREATE_TABLE) {
        r.table = get_str(p);
        r.schema.pk_index = get<int32_t>(p);
        uint32_t n = get<uint32_t>(p);
        for (uint32_t i = 0; i < n; ++i) {
            Column c;
            c.name = get_str(p);
            c.type = (TypeId)get<uint8_t>(p);
            r.schema.columns.push_back(c);
        }
    } else if (r.type == LogType::INSERT || r.type == LogType::DELETE) {
        r.table = get_str(p);
        r.tuple_bytes = get_bytes(p);
    }
    return r;
}

} // namespace

LogManager::LogManager(const std::string &log_file) : file_name_(log_file) {
    io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    if (!io_.is_open()) {
        io_.clear();
        io_.open(file_name_, std::ios::out | std::ios::binary);
        io_.close();
        io_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary | std::ios::app);
    }
    // Continue LSNs after whatever is already on disk.
    auto records = read_all();
    if (!records.empty()) next_lsn_ = records.back().lsn + 1;
}

LogManager::~LogManager() {
    if (io_.is_open()) { io_.flush(); io_.close(); }
}

lsn_t LogManager::append(LogRecord rec) {
    std::lock_guard<std::mutex> guard(latch_);
    rec.lsn = next_lsn_++;
    std::vector<char> payload = encode(rec);
    uint32_t len = (uint32_t)payload.size();
    io_.seekp(0, std::ios::end);
    io_.write(reinterpret_cast<const char *>(&len), sizeof(len));
    io_.write(payload.data(), payload.size());
    return rec.lsn;
}

void LogManager::flush() {
    std::lock_guard<std::mutex> guard(latch_);
    io_.flush(); // force buffered records out (durability point at COMMIT)
}

std::vector<LogRecord> LogManager::read_all() {
    std::vector<LogRecord> out;
    std::ifstream in(file_name_, std::ios::binary);
    if (!in.is_open()) return out;
    while (true) {
        uint32_t len;
        in.read(reinterpret_cast<char *>(&len), sizeof(len));
        if (in.gcount() < (std::streamsize)sizeof(len)) break; // clean EOF
        std::vector<char> payload(len);
        in.read(payload.data(), len);
        if (in.gcount() < (std::streamsize)len) break; // truncated tail (torn write)
        out.push_back(decode(payload.data()));
    }
    return out;
}

} // namespace minidb
