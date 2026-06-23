#include "recovery/wal.hpp"

#include <cstring>
#include <filesystem>

namespace minidb {

namespace {
void put_u32(std::vector<uint8_t>& b, uint32_t v) {
    b.insert(b.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4);
}
void put_i64(std::vector<uint8_t>& b, int64_t v) {
    b.insert(b.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 8);
}
void put_blob(std::vector<uint8_t>& b, const std::vector<uint8_t>& blob) {
    put_u32(b, static_cast<uint32_t>(blob.size()));
    b.insert(b.end(), blob.begin(), blob.end());
}
void put_str(std::vector<uint8_t>& b, const std::string& s) {
    put_u32(b, static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}
}  // namespace

WAL::WAL(const std::string& path) : path_(path) {
    out_.open(path_, std::ios::binary | std::ios::app);
    if (!out_.is_open()) throw std::runtime_error("WAL: cannot open " + path_);
}

WAL::~WAL() { if (out_.is_open()) { out_.flush(); out_.close(); } }

void WAL::append(const LogRecord& rec, bool force) {
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>(rec.type));
    put_i64(body, rec.txn);
    put_str(body, rec.table);
    put_blob(body, rec.before);
    put_blob(body, rec.after);
    put_blob(body, rec.ddl);

    uint32_t len = static_cast<uint32_t>(body.size());
    out_.write(reinterpret_cast<const char*>(&len), 4);
    out_.write(reinterpret_cast<const char*>(body.data()), body.size());
    if (force) flush();
}

void WAL::log_begin(TxnId txn)  { LogRecord r; r.type = LogType::BEGIN;  r.txn = txn; append(r, false); }
void WAL::log_commit(TxnId txn) { LogRecord r; r.type = LogType::COMMIT; r.txn = txn; append(r, true); }
void WAL::log_abort(TxnId txn)  { LogRecord r; r.type = LogType::ABORT;  r.txn = txn; append(r, true); }

void WAL::log_insert(TxnId txn, const std::string& table, const std::vector<uint8_t>& after) {
    LogRecord r; r.type = LogType::INSERT; r.txn = txn; r.table = table; r.after = after;
    append(r, false);
}
void WAL::log_update(TxnId txn, const std::string& table,
                     const std::vector<uint8_t>& before, const std::vector<uint8_t>& after) {
    LogRecord r; r.type = LogType::UPDATE; r.txn = txn; r.table = table;
    r.before = before; r.after = after; append(r, false);
}
void WAL::log_delete(TxnId txn, const std::string& table, const std::vector<uint8_t>& before) {
    LogRecord r; r.type = LogType::DELETE; r.txn = txn; r.table = table; r.before = before;
    append(r, false);
}
void WAL::log_create_table(const std::string& table, const std::vector<uint8_t>& ddl) {
    LogRecord r; r.type = LogType::CREATE_TABLE; r.table = table; r.ddl = ddl;
    append(r, true);
}

void WAL::flush() {
    out_.flush();
}

void WAL::truncate() {
    out_.close();
    out_.open(path_, std::ios::binary | std::ios::trunc);
}

std::vector<LogRecord> WAL::read_all() {
    out_.flush();
    std::vector<LogRecord> recs;
    std::ifstream in(path_, std::ios::binary);
    if (!in.is_open()) return recs;

    auto read_n = [&](void* dst, size_t n) -> bool {
        in.read(reinterpret_cast<char*>(dst), n);
        return static_cast<size_t>(in.gcount()) == n;
    };

    while (true) {
        uint32_t len;
        if (!read_n(&len, 4)) break;
        std::vector<uint8_t> body(len);
        if (!read_n(body.data(), len)) break;   // torn tail record -> stop

        size_t off = 0;
        auto u32 = [&]() { uint32_t v; std::memcpy(&v, body.data() + off, 4); off += 4; return v; };
        auto i64 = [&]() { int64_t v; std::memcpy(&v, body.data() + off, 8); off += 8; return v; };
        auto str = [&]() { uint32_t n = u32(); std::string s(reinterpret_cast<char*>(body.data() + off), n); off += n; return s; };
        auto blob = [&]() { uint32_t n = u32(); std::vector<uint8_t> b(body.begin() + off, body.begin() + off + n); off += n; return b; };

        LogRecord r;
        r.type = static_cast<LogType>(body[off++]);
        r.txn = i64();
        r.table = str();
        r.before = blob();
        r.after = blob();
        r.ddl = blob();
        recs.push_back(std::move(r));
    }
    return recs;
}

}  // namespace minidb
