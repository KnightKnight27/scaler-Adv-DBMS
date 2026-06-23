#include "recovery/wal.h"
#include <cstdint>
#include <fstream>

namespace minidb {

namespace {
void WriteU32(std::ostream &os, uint32_t v) { os.write(reinterpret_cast<char *>(&v), 4); }
void WriteI64(std::ostream &os, int64_t v) { os.write(reinterpret_cast<char *>(&v), 8); }
void WriteStr(std::ostream &os, const std::string &s) {
  WriteU32(os, static_cast<uint32_t>(s.size()));
  os.write(s.data(), s.size());
}
bool ReadU32(std::istream &is, uint32_t *v) { return static_cast<bool>(is.read(reinterpret_cast<char *>(v), 4)); }
bool ReadI64(std::istream &is, int64_t *v) { return static_cast<bool>(is.read(reinterpret_cast<char *>(v), 8)); }
bool ReadStr(std::istream &is, std::string *s) {
  uint32_t len;
  if (!ReadU32(is, &len)) return false;
  s->resize(len);
  return len == 0 || static_cast<bool>(is.read(&(*s)[0], len));
}
}  // namespace

WAL::WAL(std::string file) : file_(std::move(file)) {
  // Determine the next lsn from any existing log.
  auto recs = ReadAll();
  if (!recs.empty()) next_lsn_ = recs.back().lsn + 1;
}

WAL::~WAL() = default;

lsn_t WAL::Append(LogRecord rec) {
  rec.lsn = next_lsn_++;
  // Open in append mode and flush+close so the record is durable immediately
  // (models fsync of the log before the data page is written).
  std::ofstream out(file_, std::ios::binary | std::ios::app);
  WriteI64(out, rec.lsn);
  WriteI64(out, rec.txn);
  WriteU32(out, static_cast<uint32_t>(rec.type));
  WriteStr(out, rec.table);
  WriteI64(out, rec.key);
  WriteStr(out, rec.image);
  out.flush();
  return rec.lsn;
}

std::vector<LogRecord> WAL::ReadAll() {
  std::vector<LogRecord> recs;
  std::ifstream in(file_, std::ios::binary);
  if (!in.is_open()) return recs;
  while (true) {
    LogRecord r;
    uint32_t type;
    if (!ReadI64(in, &r.lsn)) break;
    if (!ReadI64(in, &r.txn)) break;
    if (!ReadU32(in, &type)) break;
    r.type = static_cast<LogType>(type);
    if (!ReadStr(in, &r.table)) break;
    if (!ReadI64(in, &r.key)) break;
    if (!ReadStr(in, &r.image)) break;
    recs.push_back(std::move(r));
  }
  return recs;
}

void WAL::Reset() {
  std::ofstream out(file_, std::ios::binary | std::ios::trunc);
  out.flush();
  next_lsn_ = 0;
}

}  // namespace minidb
