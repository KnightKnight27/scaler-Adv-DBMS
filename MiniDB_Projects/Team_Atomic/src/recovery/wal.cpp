#include "recovery/wal.h"
#include <cstring>
#include <unistd.h>

namespace minidb {

WAL::WAL(const std::string& path) : path_(path) {
  // Open for append+read, creating if needed.
  fp_ = std::fopen(path_.c_str(), "a+b");
  if (!fp_) throw DBError("WAL: cannot open " + path_);
  // Recover next_lsn_ from existing records.
  auto recs = ReadAll();
  if (!recs.empty()) next_lsn_ = recs.back().lsn + 1;
}

WAL::~WAL() {
  if (fp_) { std::fflush(fp_); std::fclose(fp_); }
}

static void PutInt32(std::string& b, int32_t v) { b.append(reinterpret_cast<char*>(&v), 4); }
static void PutInt64(std::string& b, int64_t v) { b.append(reinterpret_cast<char*>(&v), 8); }
static void PutStr(std::string& b, const std::string& s) {
  PutInt32(b, static_cast<int32_t>(s.size()));
  b.append(s);
}

lsn_t WAL::Append(const LogRecord& rec) {
  lsn_t lsn = next_lsn_++;
  std::string payload;
  PutInt64(payload, lsn);
  PutInt32(payload, static_cast<int32_t>(rec.type));
  PutInt64(payload, rec.txn);
  PutStr(payload, rec.table);
  PutInt64(payload, rec.key);
  PutStr(payload, rec.row);

  int32_t len = static_cast<int32_t>(payload.size());
  std::fwrite(&len, 4, 1, fp_);
  std::fwrite(payload.data(), 1, payload.size(), fp_);
  std::fflush(fp_);  // process-crash safe; COMMIT adds fsync (Flush) for power loss
  return lsn;
}

void WAL::Flush() {
  if (!fp_) return;
  std::fflush(fp_);
  ::fsync(fileno(fp_));  // durability: survive power loss, not just process crash
}

std::vector<LogRecord> WAL::ReadAll() {
  std::vector<LogRecord> out;
  std::FILE* rf = std::fopen(path_.c_str(), "rb");
  if (!rf) return out;
  while (true) {
    int32_t len;
    if (std::fread(&len, 4, 1, rf) != 1) break;
    std::string buf(len, '\0');
    if (static_cast<int32_t>(std::fread(&buf[0], 1, len, rf)) != len) break;  // torn tail
    const char* p = buf.data();
    LogRecord r;
    std::memcpy(&r.lsn, p, 8); p += 8;
    int32_t type; std::memcpy(&type, p, 4); p += 4; r.type = static_cast<LogType>(type);
    std::memcpy(&r.txn, p, 8); p += 8;
    int32_t tl; std::memcpy(&tl, p, 4); p += 4; r.table.assign(p, tl); p += tl;
    std::memcpy(&r.key, p, 8); p += 8;
    int32_t rl; std::memcpy(&rl, p, 4); p += 4; r.row.assign(p, rl); p += rl;
    out.push_back(std::move(r));
  }
  std::fclose(rf);
  return out;
}

void WAL::Truncate() {
  if (fp_) std::fclose(fp_);
  fp_ = std::fopen(path_.c_str(), "w+b");  // truncate
  if (!fp_) throw DBError("WAL: cannot truncate " + path_);
  next_lsn_ = 0;
}

}  // namespace minidb
