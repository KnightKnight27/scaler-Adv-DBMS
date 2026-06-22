#include "recovery/log_manager.h"

#include <cstring>
#include <stdexcept>

namespace minidb {

namespace {
// Serialize one record to a fixed 17-byte buffer (little-endian, field by
// field, so the on-disk format never depends on struct padding).
void Pack(const LogRecord& r, char* b) {
  b[0] = static_cast<char>(r.type);
  std::memcpy(b + 1, &r.txn, 4);
  std::memcpy(b + 5, &r.idx, 4);
  std::memcpy(b + 9, &r.before, 4);
  std::memcpy(b + 13, &r.after, 4);
}
bool Unpack(const char* b, LogRecord* r) {
  r->type = static_cast<LogType>(b[0]);
  std::memcpy(&r->txn, b + 1, 4);
  std::memcpy(&r->idx, b + 5, 4);
  std::memcpy(&r->before, b + 9, 4);
  std::memcpy(&r->after, b + 13, 4);
  return true;
}
const int kRecSize = 17;
}  // namespace

LogManager::LogManager(const std::string& path, bool truncate) {
  f_ = std::fopen(path.c_str(), truncate ? "wb" : "ab");
  if (!f_) throw std::runtime_error("LogManager: cannot open " + path);
}

LogManager::~LogManager() {
  if (f_) { std::fflush(f_); std::fclose(f_); }
}

void LogManager::Append(const LogRecord& r) {
  char b[kRecSize];
  Pack(r, b);
  std::fwrite(b, 1, kRecSize, f_);
}

void LogManager::Begin(int32_t txn)  { Append({LogType::BEGIN,  txn, 0, 0, 0}); }
void LogManager::Commit(int32_t txn) { Append({LogType::COMMIT, txn, 0, 0, 0}); Flush(); }
void LogManager::Abort(int32_t txn)  { Append({LogType::ABORT,  txn, 0, 0, 0}); Flush(); }

void LogManager::Update(int32_t txn, int32_t idx, int32_t before, int32_t after) {
  Append({LogType::UPDATE, txn, idx, before, after});
}

void LogManager::Flush() {
  if (f_) std::fflush(f_);
}

std::vector<LogRecord> LogManager::ReadAll(const std::string& path) {
  std::vector<LogRecord> out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  char b[kRecSize];
  while (std::fread(b, 1, kRecSize, f) == static_cast<size_t>(kRecSize)) {
    LogRecord r;
    Unpack(b, &r);
    out.push_back(r);
  }
  std::fclose(f);
  return out;
}

}  // namespace minidb
