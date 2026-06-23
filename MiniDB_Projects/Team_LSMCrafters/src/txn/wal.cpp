#include "txn/wal.h"

namespace minidb {
namespace {

template <typename T>
void put(std::ostream& os, const T& v) {
  os.write(reinterpret_cast<const char*>(&v), sizeof(T));
}
template <typename T>
bool get(std::istream& is, T& v) {
  return static_cast<bool>(is.read(reinterpret_cast<char*>(&v), sizeof(T)));
}

void put_bytes(std::ostream& os, const Bytes& b) {
  uint32_t len = static_cast<uint32_t>(b.size());
  put(os, len);
  os.write(b.data(), len);
}
bool get_bytes(std::istream& is, Bytes& b) {
  uint32_t len;
  if (!get(is, len)) return false;
  b.resize(len);
  return len == 0 || static_cast<bool>(is.read(b.data(), len));
}

void write_record(std::ostream& os, const LogRecord& r) {
  put(os, r.lsn);
  put(os, r.txn);
  put(os, r.type);
  put(os, r.table);
  put(os, r.key);
  put(os, r.has_before);
  put(os, r.has_after);
  put_bytes(os, r.before);
  put_bytes(os, r.after);
}

bool read_record(std::istream& is, LogRecord& r) {
  if (!get(is, r.lsn)) return false;
  return get(is, r.txn) && get(is, r.type) && get(is, r.table) && get(is, r.key) &&
         get(is, r.has_before) && get(is, r.has_after) &&
         get_bytes(is, r.before) && get_bytes(is, r.after);
}

}  // namespace

LogManager::LogManager(std::string path, bool truncate) : path_(std::move(path)) {
  std::ios::openmode mode = std::ios::out | std::ios::binary | std::ios::app;
  if (truncate) mode = std::ios::out | std::ios::binary | std::ios::trunc;
  out_.open(path_, mode);
}

LSN LogManager::append(const LogRecord& record) {
  std::lock_guard<std::mutex> lk(mu_);
  LogRecord r = record;
  r.lsn = next_lsn_++;
  buffer_.push_back(std::move(r));
  return buffer_.back().lsn;
}

void LogManager::flush_upto(LSN lsn) {
  std::lock_guard<std::mutex> lk(mu_);
  std::size_t written = 0;
  for (const LogRecord& r : buffer_) {
    if (r.lsn > lsn) break;  // buffer_ is in append (LSN) order
    write_record(out_, r);
    flushed_ = r.lsn;
    ++written;
  }
  if (written > 0) {
    out_.flush();  // push to the OS file (our crash model drops in-memory state only)
    buffer_.erase(buffer_.begin(), buffer_.begin() + written);
  }
}

std::vector<LogRecord> LogManager::read_all() {
  std::ifstream in(path_, std::ios::binary);
  std::vector<LogRecord> records;
  LogRecord r;
  while (read_record(in, r)) records.push_back(r);
  return records;
}

}  // namespace minidb
