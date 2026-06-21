#include "wal.hpp"

namespace {
void put_u32(std::ofstream& o, std::uint32_t v) { o.write(reinterpret_cast<const char*>(&v), 4); }
void put_u64(std::ofstream& o, std::uint64_t v) { o.write(reinterpret_cast<const char*>(&v), 8); }
void put_str(std::ofstream& o, const std::string& s) {
    put_u32(o, static_cast<std::uint32_t>(s.size()));
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool get_u32(std::ifstream& in, std::uint32_t& v) { return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), 4)); }
bool get_u64(std::ifstream& in, std::uint64_t& v) { return static_cast<bool>(in.read(reinterpret_cast<char*>(&v), 8)); }
bool get_str(std::ifstream& in, std::string& s) {
    std::uint32_t len;
    if (!get_u32(in, len)) return false;
    s.resize(len);
    return len == 0 || static_cast<bool>(in.read(&s[0], len));
}
}  // namespace

WAL::WAL(const std::string& path) : path_(path) {
    out_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
}

WAL::~WAL() {
    if (out_.is_open()) { out_.flush(); out_.close(); }
}

// Layout per record: type(1) txid(8) pk(4) table(len+bytes) image(len+bytes).
void WAL::append(const LogRecord& rec) {
    std::uint8_t t = static_cast<std::uint8_t>(rec.type);
    out_.write(reinterpret_cast<const char*>(&t), 1);
    put_u64(out_, rec.txid);
    put_u32(out_, static_cast<std::uint32_t>(rec.pk));
    put_str(out_, rec.table);
    put_str(out_, rec.image);
}

void WAL::flush() { out_.flush(); }

std::vector<LogRecord> WAL::read_all() {
    std::vector<LogRecord> recs;
    std::ifstream in(path_, std::ios::in | std::ios::binary);
    if (!in.is_open()) return recs;
    while (true) {
        std::uint8_t t;
        if (!in.read(reinterpret_cast<char*>(&t), 1)) break;  // clean EOF
        LogRecord r;
        r.type = static_cast<LogType>(t);
        std::uint64_t txid;
        std::uint32_t pk;
        if (!get_u64(in, txid)) break;
        if (!get_u32(in, pk)) break;
        r.txid = txid;
        r.pk = static_cast<int>(pk);
        if (!get_str(in, r.table)) break;   // a torn final record (crash mid-write)
        if (!get_str(in, r.image)) break;   // is simply dropped
        recs.push_back(std::move(r));
    }
    return recs;
}
