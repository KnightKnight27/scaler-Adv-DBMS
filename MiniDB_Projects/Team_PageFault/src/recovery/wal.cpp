#include "wal.hpp"
#include <cstring>

namespace minidb {

WAL::WAL(std::string path) : path_(std::move(path)) {
    out_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
}

// Helpers to pack/unpack fixed-width little-endian fields into a byte buffer.
static void put(std::string& b, const void* p, size_t n) {
    b.append(static_cast<const char*>(p), n);
}

void WAL::append(const LogRecord& rec) {
    std::string body;
    uint8_t type = static_cast<uint8_t>(rec.type);
    put(body, &type, 1);
    put(body, &rec.txid, sizeof(rec.txid));
    put(body, &rec.pk, sizeof(rec.pk));
    uint16_t tlen = static_cast<uint16_t>(rec.table.size());
    put(body, &tlen, sizeof(tlen));
    put(body, rec.table.data(), tlen);
    uint32_t ilen = static_cast<uint32_t>(rec.image.size());
    put(body, &ilen, sizeof(ilen));
    put(body, rec.image.data(), ilen);

    uint32_t total = static_cast<uint32_t>(body.size());
    out_.write(reinterpret_cast<const char*>(&total), sizeof(total));
    out_.write(body.data(), body.size());
    out_.flush(); // write-ahead: durable before the change is applied/forced
}

void WAL::logBegin(TxnId txid) { append({LogType::BEGIN, txid, 0, "", ""}); }

void WAL::logInsert(TxnId txid, const std::string& table, int64_t pk, const std::string& image) {
    append({LogType::INSERT, txid, pk, table, image});
}

void WAL::logDelete(TxnId txid, const std::string& table, int64_t pk, const std::string& image) {
    append({LogType::DELETE, txid, pk, table, image});
}

void WAL::logCommit(TxnId txid) { append({LogType::COMMIT, txid, 0, "", ""}); }
void WAL::logAbort(TxnId txid) { append({LogType::ABORT, txid, 0, "", ""}); }

std::vector<LogRecord> WAL::readAll() {
    std::vector<LogRecord> recs;
    std::ifstream in(path_, std::ios::in | std::ios::binary);
    if (!in.is_open()) return recs;

    while (true) {
        uint32_t total;
        in.read(reinterpret_cast<char*>(&total), sizeof(total));
        if (in.gcount() < static_cast<std::streamsize>(sizeof(total))) break; // EOF
        std::string body(total, '\0');
        in.read(&body[0], total);
        if (in.gcount() < static_cast<std::streamsize>(total)) break; // torn record

        size_t off = 0;
        LogRecord r;
        uint8_t type;
        std::memcpy(&type, body.data() + off, 1); off += 1;
        r.type = static_cast<LogType>(type);
        std::memcpy(&r.txid, body.data() + off, sizeof(r.txid)); off += sizeof(r.txid);
        std::memcpy(&r.pk, body.data() + off, sizeof(r.pk)); off += sizeof(r.pk);
        uint16_t tlen;
        std::memcpy(&tlen, body.data() + off, sizeof(tlen)); off += sizeof(tlen);
        r.table = body.substr(off, tlen); off += tlen;
        uint32_t ilen;
        std::memcpy(&ilen, body.data() + off, sizeof(ilen)); off += sizeof(ilen);
        r.image = body.substr(off, ilen); off += ilen;
        recs.push_back(std::move(r));
    }
    return recs;
}

void WAL::reset() {
    if (out_.is_open()) out_.close();
    std::ofstream trunc(path_, std::ios::out | std::ios::trunc | std::ios::binary);
    trunc.close();
    out_.open(path_, std::ios::out | std::ios::app | std::ios::binary);
}

} // namespace minidb
